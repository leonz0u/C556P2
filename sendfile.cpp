#include <iostream>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <sys/time.h>
#include <getopt.h>

using namespace std;

// Global variables for test
const int maxPayloadSize = 1450;
// Receiver window size
const int rwnd = 8;
// Congestion window size
const int cwnd = 8;
// Timeout
const int timeout_s = 1;
const int timeout_ms = 0;

// Structure for the RFTP packet
struct RFTPPacket
{
    // 32-bit sequence number
    uint32_t seqNumber;
    // 32-bit acknowledgement number
    uint32_t ackNumber;
    // 8-bit flags
    // if bit 6 is set, it is a not a data packet but a packet with file information
    // bit 6 is 01000000
    // if bit 4 is set, it is an ACK
    // bit 4 is 00010000
    // if bit 2 is set, it is the last packet
    // bit 2 is 00000010
    uint8_t flags;
    // current window size
    uint16_t windowSize;
    // 16-bit checksum for the header
    uint16_t checksum;
    // data payload
    std::vector<uint8_t> data;
};

class RFTPSender
{
private:
    struct sockaddr_in senderAddress;
    struct sockaddr_in receiverAddress;
    int senderSocket;
    std::ifstream file;
    int fileSize;
    std::string subdir;
    std::string filename;

public:
    RFTPSender();
    void initSenderSocket();
    void closeSenderSocket();
    void setReceiverAddress(string ipAddress, int portNumber);
    bool openFile(const std::string &subPath, const std::string &filename);
    void closeFile();
    uint16_t calculateChecksum(const RFTPPacket &packet);
    bool sendPacket(RFTPPacket &packet);
    void createInfoPacket(RFTPPacket &packet);
    void createSegments(int seqBegin, int segmentsNum, bool isLastPacket, vector<RFTPPacket> &senderBuffer);  
    int receiveACK(uint8_t type);
    bool sendInfoPacket();
    void sendFile();
};

//initialize the RFTPSender object
RFTPSender::RFTPSender()
{
    senderAddress.sin_family = AF_INET;
    senderAddress.sin_addr.s_addr = INADDR_ANY;
    senderAddress.sin_port = htons(0);
    senderSocket = 0;
    fileSize = 0;
    subdir = "";
    filename = "";
}

// Initialize the sender UDP socket
void RFTPSender::initSenderSocket()
{
    senderSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (senderSocket < 0)
    {
        cerr << "Error: The sender socket could not be opened!" << endl;
        exit(1);
    }
}

// Close the sender socket
void RFTPSender::closeSenderSocket()
{
    if (senderSocket > 0)
    {
        close(senderSocket);
    }
}

// get the receiver address and port number and set the receiver address
void RFTPSender::setReceiverAddress(string ipAddress, int portNumber)
{
    receiverAddress.sin_family = AF_INET;
    receiverAddress.sin_port = htons(portNumber);
    if (inet_aton(ipAddress.c_str(), &receiverAddress.sin_addr) <= 0)
    {
        cerr << "Error: Invalid IP address format!" << endl;
        exit(1);
    }
}

// open the file to be sent
bool RFTPSender::openFile(const std::string &subPath, const std::string &filename)
{
    std::string fullPath = subPath + "/" + filename;
    file.open(fullPath, std::ios::binary);
    if (!file.is_open())
    {
        cerr << "Error: Unable to open file!" << endl;
        return false;
    }
    // set subdir and filename
    subdir = subPath;
    this->filename = filename;
    // get the file size
    file.seekg(0, std::ios_base::end);
    fileSize = file.tellg();
    return true;
}

// close the file
void RFTPSender::closeFile()
{
    if (file.is_open())
    {
        file.close();
    }
}

// calculate the checksum for the packet
uint16_t RFTPSender::calculateChecksum(const RFTPPacket &packet)
{
    uint32_t sum = 0;
    sum += packet.seqNumber;
    sum += packet.ackNumber;
    sum += packet.flags;
    sum += packet.windowSize;
    for (uint8_t byte : packet.data)
    {
        sum += byte;
    }
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~sum;
}

// send the packet
bool RFTPSender::sendPacket(RFTPPacket &packet)
{
    int no = sendto(senderSocket, &packet, sizeof(packet), 0, (struct sockaddr *)&receiverAddress, sizeof(receiverAddress));
    if (no < 0)
    {
        cerr << "Error: Sending Packet failed!" << endl;
        return false;
    }
    return true;
}

// create the packet with file information
void RFTPSender::createInfoPacket(RFTPPacket &packet)
{
    packet.seqNumber = 0;
    packet.ackNumber = 0;
    // set the bit 6 to 1
    // bit 6 is 01000000
    packet.flags |= 0x40;
    packet.windowSize = 0;
    // copy the subdir and filename to the data field
    std::string filename = subdir + "/" + this->filename;
    packet.data.resize(filename.size() + 1);
    memcpy(packet.data.data(), filename.c_str(), filename.size() + 1);
    packet.checksum = calculateChecksum(packet);
}

// ceate the list of packets to be sent
void RFTPSender::createSegments(int seqBegin, int segmentsNum, bool isLastPacket, vector<RFTPPacket> &senderBuffer)
{
    // read the all file content in the segments, reduce i/o operations
    uint8_t fileContent[segmentsNum * maxPayloadSize];
    file.seekg(seqBegin * maxPayloadSize);
    file.read(reinterpret_cast<char *>(fileContent), segmentsNum * maxPayloadSize);
    cout << "Read " << file.gcount() << " bytes from file" << endl;
    for (int i = 0; i < segmentsNum; ++i)
    {
        RFTPPacket packet;
        packet.seqNumber = seqBegin + i;
        packet.ackNumber = 0;
        packet.flags = 0;
        packet.windowSize = min(rwnd, cwnd);
        packet.data.resize(maxPayloadSize);
        packet.checksum = 0;
        // if it is the last packet, set the bit 2 to 1
        if (isLastPacket)
        {
            // bit 2 is 00000100
            packet.flags |= 0x04;
            memcpy(packet.data.data(), fileContent, file.gcount());
            packet.data.resize(file.gcount());
            
        }
        else
        {
            memcpy(packet.data.data(), fileContent + i * maxPayloadSize, maxPayloadSize);
        }
        packet.checksum = calculateChecksum(packet);
        // store the packet according to the sequence number
        senderBuffer[(seqBegin + i) % senderBuffer.size()] = packet;
    }
}

// receive the ACK
int RFTPSender::receiveACK(uint8_t type)
{
    // ACK Type is 00010000
    // last packet ACK type is 00010010
    // information packet ACK type is 01010000
    RFTPPacket ack;
    int no = recvfrom(senderSocket, &ack, sizeof(ack), 0, (struct sockaddr *)&receiverAddress, (socklen_t *)sizeof(receiverAddress));
    // validate the ACK using Checksum
    if(ack.checksum != calculateChecksum(ack))
    {
        return -1;
    }
    //check packet if belongs to the type
    // for type 00010000, allow 00010000 and 00010010
    // mask bit 2 using 11111101 (0xFD)
    // for type 01010000, only allow 01010000
    if((ack.flags & 0xFD) == type)
    {
        return ack.ackNumber;
    }
    else if(ack.flags == type)
    {
        return ack.ackNumber;
    }
    return -1;
}

// send the information packet
bool RFTPSender::sendInfoPacket()
{
    bool isFinished = false;
    RFTPPacket infoPacket;
    createInfoPacket(infoPacket);
    while(!isFinished)
    {
        if(!sendPacket(infoPacket))
        {
            cerr << "Error: Sending Information Packet failed!" << endl;
            continue;
        }
        cout << "Sent information packet with filename: " << filename << endl;
        // wait for the ACK
        struct timeval tv;
        tv.tv_sec = timeout_s;
        tv.tv_usec = timeout_ms * 1000;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(senderSocket, &fds);
        int activity = select(senderSocket + 1, &fds, NULL, NULL, &tv);
        if (activity > 0 && FD_ISSET(senderSocket, &fds))
        {
            // receive the ACK for the information packet
            // bit 6 is set to 1
            // bit 4 is set to 1
            // type is 01010000
            int ack = receiveACK(0x50);
            if (ack < 0)
            {
                cerr << "Error: Receiving ACK failed!" << endl;
                continue;
            }
            cout << "Received ACK of information packet: " << ack << endl;
            isFinished = true;
            break;
        }
        else if (activity == 0)
        {
            cout << "Timeout!" << endl;
            continue;
        }
        else
        {
            cerr << "Error: Select failed!" << endl;
            continue;
        }
    }
    return isFinished;
}

// send the file
void RFTPSender::sendFile()
{
    int seqBegin = 0;
    int segmentsNum = 0;
    int maxAck = -1;
    int totalWindowSize = min(cwnd, rwnd);
    int usedWindowSize = 0;
    int remainingFileSize = fileSize;
    // buffer to store the packets to be sent, the size is the total window size
    vector<RFTPPacket> senderBuffer(totalWindowSize);
    // set the retransmission timeout
    struct timeval tv;
    tv.tv_sec = timeout_s;
    tv.tv_usec = timeout_ms * 1000;        // convert timeout to microseconds
    // struct timeval t1, t2;
    // set the socket
    bool finished = false;
    while (!finished)
    {
        // create the list of packets to be sent if the window is not full
        while (usedWindowSize < totalWindowSize && remainingFileSize > 0)
        {
            if (remainingFileSize < maxPayloadSize)
            {
                segmentsNum = 1;
                createSegments(seqBegin, segmentsNum, true, senderBuffer);
            }
            else
            {
                segmentsNum = totalWindowSize - usedWindowSize;
                createSegments(seqBegin, segmentsNum, false, senderBuffer);
                if(remainingFileSize == maxPayloadSize * segmentsNum)
                {
                    // set the bit 2 to 1 for the last packet
                    // bit 2 is 00000100
                    senderBuffer[(seqBegin + segmentsNum - 1) % senderBuffer.size()].flags |= 0x04;
                }
            }
            // send the packets
            // gettimeofday(&t1, NULL);
            for (int i = 0; i < segmentsNum; ++i)
            {
                if (!sendPacket(senderBuffer[(seqBegin + i) % senderBuffer.size()]))
                {
                    cerr << "Error: Sending Packet failed!" << endl;
                    continue;
                }
                cout << "Sent packet with sequence number: " << seqBegin + i << endl;
            }
            usedWindowSize += segmentsNum;
        }

        // wait for the ACK or timeout
        // using select to aoivd blocking
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(senderSocket, &fds);
        int activity = select(senderSocket + 1, &fds, NULL, NULL, &tv);
        //FD_SET(senderSocket, &fds) is used to check if we have received any data from the socket
        if (activity > 0 && FD_ISSET(senderSocket, &fds))
        {
            // receive the ACK
            // bit 4 is set to 1
            // type is 00010000
            int ack = receiveACK(0x10);
            if (ack < 0)
            {
                cerr << "Error: Receiving ACK failed!" << endl;
                continue;
            }
            cout << "Received ACK: " << ack << endl;
            // update the window size
            usedWindowSize = max(0, usedWindowSize - (ack - maxAck));
            maxAck = max(maxAck, ack);
            // update the sequence number
            seqBegin = maxAck + 1;
            remainingFileSize = fileSize - seqBegin * maxPayloadSize;
            if(remainingFileSize <= 0)
            {
                finished = true;
                break;
            }
        }
        else if (activity == 0)
        {
            // timeout
            cout << "Timeout!" << endl;
            //retransmit the packets in the window
            for (int i = 0; i < usedWindowSize; ++i)
            {
                if (!sendPacket(senderBuffer[(seqBegin + i) % senderBuffer.size()]))
                {
                    cerr << "Error: Sending Packet failed!" << endl;
                    continue;
                }
                cout << "Retransmitted packet with sequence number: " << seqBegin + i << endl;
            }
            usedWindowSize = 0;
            seqBegin = maxAck + 1;
            remainingFileSize = fileSize - seqBegin * maxPayloadSize;
        }
        else
        {
            cerr << "Error: Select failed!" << endl;
            continue;
        }
    }
}

int main(int argc, char *argv[])
{
    string recvHost;
    int recvPort = 0;
    string subdir;
    string filename;

    // int opt;
    // while ((opt = getopt(argc, argv, "r:f:")) != -1)
    // {
    //     switch (opt)
    //     {
    //     case 'r':
    //     {
    //         string addr = optarg;
    //         size_t colon_pos = addr.find(":");
    //         if (colon_pos == std::string::npos)
    //         {
    //             std::cerr << "Invalid receiver address format. Expected <recv host>:<recv port>" << std::endl;
    //             return 1;
    //         }
    //         recvHost = addr.substr(0, colon_pos);
    //         recvPort = std::stoi(addr.substr(colon_pos + 1));
    //         break;
    //     }
    //     case 'f':
    //     {
    //         string file = optarg;
    //         size_t slash_pos = file.find("/");
    //         if (slash_pos == std::string::npos)
    //         {
    //             std::cerr << "Invalid file information format. Expected <subdir>/<filename>" << std::endl;
    //             return 1;
    //         }
    //         subdir = file.substr(0, slash_pos);
    //         filename = file.substr(slash_pos + 1);
    //         break;
    //     }
    //     default:
    //         std::cerr << "Usage: sendfile -r <recv host>:<recv port> -f <subdir>/<filename>" << std::endl;
    //         return 1;
    //     }
    // }


    //for local test purpose sendfile -r 128.42.124.187:18105 -f test.txt
    recvHost = "128.42.124.187";
    recvPort = 18105;
    // subdir = "send";
    subdir = ".";
    filename = "T_11600B.bin";


    if (recvHost.empty() || recvPort == 0 || filename.empty())
    {
        std::cerr << "Usage: sendfile -r <recv host>:<recv port> -f <subdir>/<filename>" << std::endl;
        return 1;
    }


    RFTPSender sender;
    sender.initSenderSocket();
    sender.setReceiverAddress(recvHost, recvPort);

    if (!sender.openFile(subdir, filename))
    {
        cerr << "Error: Unable to open file!" << endl;
        return 1;
    }

    if (!sender.sendInfoPacket())
    {
        cerr << "Error: Sending Information Packet failed!" << endl;
        return 1;
    }


    sender.sendFile();
    sender.closeFile();

    return 0;
}