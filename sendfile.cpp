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
const int rwnd = 32;
// Congestion window size
const int cwnd = 32;
// Timeout
const int timeout_s = 0;
const int timeout_ms = 2;
// Timeout for information packet
const int timeout_info_s = 1;
const int timeout_info_ms = 0;

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
    // bit 2 is 00000100
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
    void clearSocketBuffer(int socket);
    void closeSenderSocket();
    void setReceiverAddress(string ipAddress, int portNumber);
    bool openFile(const std::string &subPath, const std::string &filename);
    void closeFile();
    uint16_t calculateChecksum(const RFTPPacket &packet);
    std::vector<uint8_t> serializePacket(const RFTPPacket& packet);
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

// clear the socket buffer
void RFTPSender::clearSocketBuffer(int socket)
{
    char buffer[1472];
    while (recv(socket, buffer, sizeof(buffer), MSG_DONTWAIT) > 0)
    {
        // clear the buffer
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
    // uint32_t seqNumber
    sum += (packet.seqNumber >> 16) & 0xFFFF;  // High 16 bits
    sum += packet.seqNumber & 0xFFFF;          // Low 16 bits    
    // uint32_t ackNumber
    sum += (packet.ackNumber >> 16) & 0xFFFF;  // High 16 bits
    sum += packet.ackNumber & 0xFFFF;          // Low 16 bits
    // uint8_t flags
    sum += packet.flags;
    // uint16_t windowSize
    sum += packet.windowSize;
    //if the data is not empty, calculate the checksum for the data
    if (!packet.data.empty())
    {
        // put uint8_t data into uint32_t sum
        int packetDataSize = packet.data.size();
        for (int i = 0; i < packetDataSize; i += 2)
        {
            uint16_t data = packet.data[i];
            if (i + 1 < packetDataSize)
            {
                data = (data << 8) + packet.data[i + 1];
            }
            sum += data;
        }
    }
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return static_cast<uint16_t>(~sum & 0xFFFF);
}


// serialize the packet
std::vector<uint8_t> RFTPSender::serializePacket(const RFTPPacket& packet)
{
    int bufferSize = sizeof(packet.seqNumber) + sizeof(packet.ackNumber) + sizeof(packet.flags) + sizeof(packet.windowSize) + sizeof(packet.checksum) + packet.data.size();
    std::vector<uint8_t> buffer(bufferSize);
    size_t offset = 0;
    memcpy(buffer.data() + offset, &packet.seqNumber, sizeof(packet.seqNumber));
    offset += sizeof(packet.seqNumber);
    memcpy(buffer.data() + offset, &packet.ackNumber, sizeof(packet.ackNumber));
    offset += sizeof(packet.ackNumber);
    memcpy(buffer.data() + offset, &packet.flags, sizeof(packet.flags));
    offset += sizeof(packet.flags);
    memcpy(buffer.data() + offset, &packet.windowSize, sizeof(packet.windowSize));
    offset += sizeof(packet.windowSize);
    memcpy(buffer.data() + offset, &packet.checksum, sizeof(packet.checksum));
    offset += sizeof(packet.checksum);
    memcpy(buffer.data() + offset, packet.data.data(), packet.data.size());
    return buffer;
}

// send the packet
bool RFTPSender::sendPacket(RFTPPacket &packet)
{
    // serialize the packet
    std::vector<uint8_t> buffer = serializePacket(packet);
    int no = sendto(senderSocket, buffer.data(), buffer.size(), 0, (struct sockaddr *)&receiverAddress, sizeof(receiverAddress));
    // int no = sendto(senderSocket, &packet, sizeof(packet), 0, (struct sockaddr *)&receiverAddress, sizeof(receiverAddress));
    if (no < 0)
    {
        return false;
    }
    // When sendfile sends a packet (including retransmission), it should print the following: [send data] start (length)
    // where start is the beginning offset (in byte) of the file sent in the packet, and length (in byte) is the amount of the file sent in that packet.
    cerr << "[send data] start " << packet.seqNumber * maxPayloadSize << " (" << packet.data.size() << ")" << endl;
    return true;
}

// create the packet with file information
void RFTPSender::createInfoPacket(RFTPPacket &packet)
{
    packet.seqNumber = 0;
    packet.ackNumber = 0;
    // set the bit 6 to 1
    // bit 6 is 01000000
    packet.flags = 0x40;
    packet.windowSize = 8;
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
    int readFileSize = file.gcount();
    // cout << "Read file size: " << readFileSize << endl;
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
        if (isLastPacket && i == segmentsNum - 1)
        {
            // bit 2 is 00000100
            packet.flags |= 0x04;
            int lastPacketSize = readFileSize - i * maxPayloadSize;
            memcpy(packet.data.data(), fileContent + i * maxPayloadSize, lastPacketSize);
            packet.data.resize(lastPacketSize);
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
    // create buffer to store the ACK
    // Ack Packet size is 13 bytes
    std::vector<uint8_t> buffer(13);
    int recvSize = recvfrom(senderSocket, buffer.data(), buffer.size(), 0, (struct sockaddr *)&receiverAddress, (socklen_t *)&receiverAddress);
    uint32_t seqNumber = 0;
    memcpy(&seqNumber, buffer.data(), sizeof(uint32_t));
    // if seqNumber is not 0, return -1
    if (seqNumber != 0)
    {
        // cerr << "Error: The ACK packet is corrupted!" << endl;
        return -1;
    }

    // validate the ACK using Checksum
    // uint32_t sum = 0;
    uint64_t sum = 0;
    uint16_t checksum = 0;
    memcpy(&checksum, buffer.data() + 11, sizeof(uint16_t));

    // add the header fields to the sum
    uint32_t ackNumber = 0;
    memcpy(&ackNumber, buffer.data() + 4, sizeof(uint32_t));
    sum += ackNumber;
    uint8_t flags = 0;
    memcpy(&flags, buffer.data() + 8, sizeof(uint8_t));
    sum += flags;
    uint16_t windowSize = 0;
    memcpy(&windowSize, buffer.data() + 9, sizeof(uint16_t));
    sum += windowSize;

    // check if has carry
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    // if the checksum is not 0xFFFF, the packet is corrupted
    if (sum + checksum != 0xFFFF)
    {
        // cerr << "Error: The ACK packet is corrupted!" << endl;
        return -1;
    }
    // check if ackNumber in the range
    if(ackNumber < 0 || ackNumber > fileSize)
    {
        // cerr << "Error: The ACK packet is out of range!" << endl;
        return -1;
    }
    //check packet if belongs to the type
    // for type 00010000, allow 00010000 and 00010100
    // mask bit 2 using 11111011(0xFB)
    // for type 01010000, only allow 01010000
    if((flags & 0xFB) == type)
    {
        return ackNumber;
    }
    else if(flags == type)
    {
        return ackNumber;
    }
    else
    {
        // cerr << "Error: The ACK packet is not the expected type!" << endl;
        return -1;
    }
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
        cerr << "Sent information packet with filename: " << filename << endl;
        // wait for the ACK
        struct timeval tv;
        tv.tv_sec = timeout_info_s;
        tv.tv_usec = timeout_info_ms * 1000;        // convert timeout to microseconds
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(senderSocket, &fds);
        int activity = select(senderSocket + 1, &fds, NULL, NULL, &tv);
        if (activity > 0 && FD_ISSET(senderSocket, &fds))
        {
            // receive the ACK for the information packet
            // bit 6 is set to 1
            // bit 4 is set to 1
            // the type is 01010000
            int ack = receiveACK(0x50);
            if (ack < 0)
            {
                // cerr << "Error: Receiving ACK failed!" << endl;
                continue;
            }
            // cout << "Received ACK of information packet: " << ack << endl;
            isFinished = true;
            // clear the receiver socket buffer
            clearSocketBuffer(senderSocket);
            break;
        }
        else if (activity == 0)
        {
            // cerr << "Ack Information packet timeout!" << endl;
            continue;
        }
        else
        {
            // cerr << "Error: Select failed!" << endl;
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
    int usedSegmentSize = 0;
    int remainingFileSize = fileSize;
    int lastSeqNumber = (fileSize + maxPayloadSize - 1) / maxPayloadSize - 1;
    // buffer to store the packets to be sent, the size is the total window size
    vector<RFTPPacket> senderBuffer(totalWindowSize);
    // set the retransmission timeout
    struct timeval tv;
    tv.tv_sec = timeout_s;
    tv.tv_usec = timeout_ms * 1000;        // convert timeout to microseconds
    bool finished = false;
    // load all packets in the window
    bool notLoaded = true;
    while (!finished)
    {
        // create the list of packets to be sent in the window
        // store the packets in the senderBuffer
        while (usedSegmentSize < totalWindowSize && seqBegin <= lastSeqNumber && notLoaded)
        {
            if (remainingFileSize < maxPayloadSize)
            {
                segmentsNum = 1;
                // createSegments(seqBegin, segmentsNum, true, senderBuffer);
                // create Segments, start from seqBegin+usedSegmentSize
                createSegments(seqBegin+usedSegmentSize, segmentsNum, true, senderBuffer);
                remainingFileSize = 0;
                usedSegmentSize += segmentsNum;
                notLoaded = false;
                break;
            }
            else
            {
                segmentsNum = totalWindowSize - usedSegmentSize;
                // if the remaining file size is less than the available window size, set the bit 2 to 1 for the last packet
                if(remainingFileSize <= maxPayloadSize * segmentsNum)
                {
                    // improve the calculation of segmentsNum
                    // segmentsNum = remainingFileSize / maxPayloadSize + (remainingFileSize % maxPayloadSize == 0 ? 0 : 1);
                    segmentsNum = (remainingFileSize + maxPayloadSize - 1) / maxPayloadSize;
                    // set the bit 2 to 1 for the last packet
                    // bit 2 is 00000100
                    // create Segments, start from seqBegin+usedSegmentSize
                    createSegments(seqBegin+usedSegmentSize, segmentsNum, true, senderBuffer);
                    remainingFileSize = 0;
                    usedSegmentSize += segmentsNum;
                    notLoaded = false;
                    break;
                }
                else
                {
                    // create Segments, start from seqBegin+usedSegmentSize
                    createSegments(seqBegin+usedSegmentSize, segmentsNum, false, senderBuffer);
                    remainingFileSize -= maxPayloadSize * segmentsNum;
                    usedSegmentSize += segmentsNum;
                }
            }
        }
        // send the packets in the window
        // from seqBegin to usedSegmentSize + segmentsNum
        while(usedWindowSize < usedSegmentSize)
        {
            if(!sendPacket(senderBuffer[(seqBegin + usedWindowSize) % senderBuffer.size()]))
            {
                cerr << "Error: Sending Packet failed!" << endl;
                continue;
            }
            // cout << "Sent packet with sequence number: " << seqBegin + usedWindowSize << endl;
            usedWindowSize++;
        }

        // wait for the ACK or timeout
        // using select to aoivd blocking
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(senderSocket, &fds);
        tv.tv_sec = timeout_s;
        tv.tv_usec = timeout_ms * 1000;        // convert timeout to microseconds
        int activity = select(senderSocket + 1, &fds, NULL, NULL, &tv);
        //FD_SET(senderSocket, &fds) is used to check if we have received any data from the socket
        if(activity == 0)
        {
            // timeout
            cerr << "Timeout!" << endl;
            //retransmit the packets in the window
            seqBegin = maxAck + 1;
            // free the used window size, but keep the used segment size
            usedWindowSize = 0;
            continue;
        }
        if(activity < 0)
        {
            cerr << "Error: Select failed!" << endl;
            continue;
        }
        if(activity > 0 && FD_ISSET(senderSocket, &fds))
        {
            // receive the ACK
            // bit 4 is set to 1
            // type is 00010000
            int ack = receiveACK(0x10);
            if (ack < 0)
            {
                // cerr << "Error: Receiving ACK failed!" << endl;
                continue;
            }
            // if the ack is not in the window, ignore it
            if (ack < seqBegin || ack >= seqBegin + totalWindowSize)
            {
                // cerr << "Received ACK: " << ack << " is not in the window!" << endl;
                continue;
            }
            // cout << "Received ACK: " << ack << endl;
            // if the ack is in the window, update the window size
            usedWindowSize = max(0, usedWindowSize - (ack - seqBegin + 1));
            usedSegmentSize = usedWindowSize;
            maxAck = (maxAck > ack ? maxAck : ack);
            // if the ack is the last packet, the file is sent successfully
            if (ack == lastSeqNumber)
            {
                finished = true;
                break;
            }
            // update the sequence number
            seqBegin = maxAck + 1;
        }
    }
    if(finished)
    {
        cout << "[completed]" << endl;
    }
    else
    {
        cerr << "Error: File sending failed!" << endl;
    }
}

int main(int argc, char *argv[])
{
    string recvHost;
    int recvPort = 0;
    string subdir;
    string filename;

    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "r:f:")) != -1)
    {
        switch (opt)
        {
        case 'r':
        {
            // Expecting recvHost:recvPort format
            char *token = strtok(optarg, ":");
            if (token != nullptr)
            {
                recvHost = token;
                token = strtok(nullptr, ":");
                if (token != nullptr)
                {
                    recvPort = stoi(token);
                }
                else
                {
                    cerr << "Error: Invalid format for -r. Expected <recv host>:<recv port>" << endl;
                    return 1;
                }
            }
            else
            {
                cerr << "Error: Invalid format for -r. Expected <recv host>:<recv port>" << endl;
                return 1;
            }
            break;
        }
        case 'f':
            // Expecting subdir/filename format
            filename = optarg;
            break;
        default:
            cerr << "Usage: " << argv[0] << " -r <recv host>:<recv port> -f <subdir>/<filename>" << endl;
            return 1;
        }
    }

    // If any required parameter is missing, show usage
    if (recvHost.empty() || recvPort == 0 || filename.empty())
    {
        cerr << "Usage: " << argv[0] << " -r <recv host>:<recv port> -f <subdir>/<filename>" << endl;
        return 1;
    }

    // Extract subdir and filename
    size_t lastSlash = filename.find_last_of("/");
    if (lastSlash != string::npos)
    {
        subdir = filename.substr(0, lastSlash);
        filename = filename.substr(lastSlash + 1);
    }
    else
    {
        subdir = "."; // If no subdirectory is specified, use current directory
    }


    RFTPSender sender;
    sender.initSenderSocket();
    sender.setReceiverAddress(recvHost, recvPort);

    if (!sender.openFile(subdir, filename))
    {
        return 1;
    }

    if (!sender.sendInfoPacket())
    {
        cerr << "Error: Sending Information Packet failed!" << endl;
        return 1;
    }

    sender.sendFile();
    sender.closeFile();
    sender.closeSenderSocket();

    return 0;
}