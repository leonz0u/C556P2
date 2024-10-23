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
#include <set>
#include <getopt.h>
#include <chrono>
#include <iomanip>
#include <map>
#include <sys/select.h> 

using namespace std;

const int maxPayloadSize = 1450;
const int rwnd = 32;              
const int cwnd = 32;
const int timeout_s = 0;
const int timeout_ms = 2;
const int maxPacketSize = 1472;
const int checksumOffset = 11;
const int infoAckNum = 4;
const int lastAckNum = 4;
const int maxwindowsize = 32;

struct RFTPPacket
{
    uint32_t seqNumber;          
    uint32_t ackNumber;          
    uint8_t flags;             
    uint16_t windowSize;       
    uint16_t checksum;           
    std::vector<uint8_t> data; 
};

class RFTPReceiver
{
private:
    struct sockaddr_in receiverAddress; 
    struct sockaddr_in senderAddress;   
    int receiverSocket;                
    std::ofstream file;                
    int fileSize;
    std::string subdir;
    std::string filename;    
    uint32_t totalBytesReceived;     
    std::chrono::steady_clock::time_point startTime;

public:
    RFTPReceiver();                  
    void initReceiverSocket(int portNumber);
    void clearSocketBuffer(int socket);
    bool openFile(const std::string &subPath, const std::string &filename); 
    void closeFile();              
    bool receivePacket(RFTPPacket &packet,uint8_t type);
    bool receivePacketWithTimeout(RFTPPacket &packet, int timeout_sec); 
    void writeFileChunk(const RFTPPacket &packet); 
    void sendAck(uint32_t ackNumber, uint8_t flag); 
    uint16_t calculateChecksum(const RFTPPacket &packet); 
    void printStatistics();           
    int getReceiverSocket();            
    void closeReceiverSocket();
    uint16_t verifyChecksum(const std::vector<uint8_t> &buffer);
};

RFTPReceiver::RFTPReceiver() : totalBytesReceived(0)
{
    receiverAddress.sin_family = AF_INET;        
    receiverAddress.sin_addr.s_addr = INADDR_ANY; 
    receiverSocket = 0;                         
}

void RFTPReceiver::initReceiverSocket(int portNumber)
{
    receiverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (receiverSocket < 0)
    {
        cerr << "Error: The receiver socket could not be opened!" << endl;
        exit(1);
    }
    
    receiverAddress.sin_port = htons(portNumber); 

    if (bind(receiverSocket, (struct sockaddr *)&receiverAddress, sizeof(receiverAddress)) < 0)
    {
        cerr << "Error: Binding failed!" << endl;
        exit(1);
    }
}

// clear the socket buffer
void RFTPReceiver::clearSocketBuffer(int socket)
{
    char buffer[1472];
    while (recv(socket, buffer, sizeof(buffer), MSG_DONTWAIT) > 0)
    {
        // clear the buffer
    }
}

bool RFTPReceiver::openFile(const std::string &subPath, const std::string &filename)
{

    // debug mode
    // std::string fullPath = "./recv/" + subPath + "/" + filename; 
    // std::string mkdirCommand = "mkdir -p ./recv/" + subPath;

    // build mode
    std::string fullPath = "./" + subPath + "/" + filename; 
    std::string mkdirCommand = "mkdir -p ./" + subPath;

    system(mkdirCommand.c_str());

    file.open(fullPath, std::ios::binary); 
    if (!file.is_open())
    {
        cerr << "Error: Unable to open file for writing!" << endl;
        return false;
    }
    // begin to record the time
    startTime = std::chrono::steady_clock::now();
    return true;
}

RFTPPacket deserializeRFTPPacket(const std::vector<uint8_t>& buffer) {
    RFTPPacket packet;
    size_t offset = 0;

    std::memcpy(&packet.seqNumber, buffer.data() + offset, sizeof(packet.seqNumber));
    offset += sizeof(packet.seqNumber);

    std::memcpy(&packet.ackNumber, buffer.data() + offset, sizeof(packet.ackNumber));
    offset += sizeof(packet.ackNumber);

    packet.flags = buffer[offset];
    offset += sizeof(packet.flags);

    std::memcpy(&packet.windowSize, buffer.data() + offset, sizeof(packet.windowSize));
    offset += sizeof(packet.windowSize);

    std::memcpy(&packet.checksum, buffer.data() + offset, sizeof(packet.checksum));
    offset += sizeof(packet.checksum);

    packet.data.resize(buffer.size() - offset);
    std::memcpy(packet.data.data(), buffer.data() + offset, packet.data.size());

    return packet;
}

void RFTPReceiver::closeFile()
{
    if (file.is_open())
    {
        file.close();
    }
}

void RFTPReceiver::closeReceiverSocket()
{
    if (receiverSocket > 0)
    {
        close(receiverSocket);
    }
}

bool RFTPReceiver::receivePacket(RFTPPacket &packet, uint8_t type)
{   
    std::vector<uint8_t> buffer(maxPacketSize);

    socklen_t senderLen = sizeof(senderAddress);  
    
    int bytesReceived = recvfrom(receiverSocket, buffer.data(), maxPacketSize, 0, (struct sockaddr *)&senderAddress, &senderLen);
    
    buffer.resize(bytesReceived);

    if (verifyChecksum(buffer))
    {
        packet = deserializeRFTPPacket(buffer);
        // check packet if belongs to its type
        // if type is 00000000, allow 00000000 and 00000100
        // mask bit 2 using 11111011(0xFB)
        // if type is 01000000, allow 01000000
        if((packet.flags & 0xFB) == type){
            return true;
        }
        else if(packet.flags == type){
            return true;
        }
        else{
            std::cerr << "Packet type mismatch!, but checksum is correct" << std::endl;
            return false;
        }
        
    }
    else
    {
        std::cerr << "Checksum mismatch, packet corrupted!" << std::endl;
        return false;
    }

    if (bytesReceived < 0)
    {
        cerr << "Error: Receiving Packet failed!" << endl;
        return false;
    }
    return true;
}

bool RFTPReceiver::receivePacketWithTimeout(RFTPPacket &packet, int timeout_sec)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(receiverSocket, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    int activity = select(receiverSocket + 1, &fds, NULL, NULL, &tv);
    if (activity < 0)
    {
        cerr << "Error: Select failed!" << endl;
        return false;
    }
    else if (activity == 0)
    {
        return false;
    }
    else
    {
        return receivePacket(packet, 0);
    }
}

void RFTPReceiver::writeFileChunk(const RFTPPacket &packet)
{
    file.write(reinterpret_cast<const char*>(packet.data.data()), packet.data.size());
    totalBytesReceived += packet.data.size(); 
}

void RFTPReceiver::sendAck(uint32_t ackNumber, uint8_t flag)
{
    RFTPPacket ackPacket;             
    ackPacket.seqNumber = 0;    
    ackPacket.ackNumber = ackNumber;  
    ackPacket.flags = flag;          
    ackPacket.windowSize = min(rwnd, cwnd);    
    ackPacket.data.resize(0);    
    ackPacket.checksum = calculateChecksum(ackPacket);

    // store ackPacket to buffer
    // Ack Packet size is 13 bytes
    std::vector<uint8_t> buffer(13);
    size_t offset = 0;
    // Add seqNumber to buffer
    std::memcpy(buffer.data() + offset, &ackPacket.seqNumber, sizeof(ackPacket.seqNumber));
    offset += sizeof(ackPacket.seqNumber);
    // Add ackNumber to buffer
    std::memcpy(buffer.data() + offset, &ackPacket.ackNumber, sizeof(ackPacket.ackNumber));
    offset += sizeof(ackPacket.ackNumber);
    // Add flags to buffer
    std::memcpy(buffer.data() + offset, &ackPacket.flags, sizeof(ackPacket.flags));
    offset += sizeof(ackPacket.flags);
    // Add windowSize to buffer
    std::memcpy(buffer.data() + offset, &ackPacket.windowSize, sizeof(ackPacket.windowSize));
    offset += sizeof(ackPacket.windowSize);
    // Add checksum to buffer
    std::memcpy(buffer.data() + offset, &ackPacket.checksum, sizeof(ackPacket.checksum));

    // send buffer to sender
    sendto(receiverSocket, buffer.data(), buffer.size(), 0, (struct sockaddr *)&senderAddress, sizeof(senderAddress));
}

uint16_t RFTPReceiver::calculateChecksum(const RFTPPacket &packet)
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

uint16_t RFTPReceiver::verifyChecksum(const std::vector<uint8_t> &buffer)
{
    uint16_t receivedChecksum = 0;
    memcpy(&receivedChecksum, buffer.data() + checksumOffset, sizeof(uint16_t));

    uint64_t sumX = 0;
    const size_t windowSizeOffset = 9;
    sumX += buffer[windowSizeOffset] + (buffer[windowSizeOffset + 1] << 8);

    uint64_t sum = 0;

    uint32_t seqNumber = 0;
    memcpy(&seqNumber, buffer.data() + 0, sizeof(uint32_t));
    sum += seqNumber;

    uint32_t ackNumber = 0;
    memcpy(&ackNumber, buffer.data() + 4, sizeof(uint32_t));
    sum += ackNumber;

    uint8_t flag = 0;
    memcpy(&flag, buffer.data() + 8, sizeof(uint8_t));
    sum += flag;

    uint16_t windowSize = 0;
    memcpy(&windowSize, buffer.data() + 9, sizeof(uint16_t));
    sum += windowSize;

    int dataSize = buffer.size() - 13;
    std::vector<uint8_t> data(dataSize);
    
    memcpy(data.data(), buffer.data() + 13, dataSize);

    int sizeOfData = data.size();
    if (!data.empty())
    {
        // put uint8_t data into uint32_t sum
        for (int i = 0; i < sizeOfData; i += 2)
        {
            uint16_t dataX = data[i];
            if (i + 1 < sizeOfData)
            {
                dataX = (dataX << 8) + data[i + 1];
            }
            sum += dataX;
        }
    }

    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // uint16_t calculatedChecksum = ~sum & 0xFFFF;

    return (sum + receivedChecksum == 0xFFFF);
}


void RFTPReceiver::printStatistics()
{
    auto endTime = std::chrono::steady_clock::now(); 
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    double durationSec = duration.count() / 1000.0; 

    cout << fixed << setprecision(2);
    cout << "Total bytes received: " << totalBytesReceived << " bytes" << endl;
    cout << "Transfer time: " << durationSec << " seconds" << endl;
    cout << "Throughput: " << (totalBytesReceived * 8.0 / 1000000.0) / durationSec << " Mbps" << endl;
}

// getReceiverSocket
int RFTPReceiver::getReceiverSocket()
{
    return receiverSocket;
}

int main(int argc, char *argv[]) {
    int port = 0; // Receiving port, initialized to 0
    // Parse command-line arguments, only accepting -p <port>
    int opt;
    while ((opt = getopt(argc, argv, "p:")) != -1)
    {
        switch (opt)
        {
            case 'p':
                if (optarg != NULL)
                {
                    port = atoi(optarg); // Convert the port string to an integer
                }
                else
                {
                    cerr << "Option -p requires an argument." << endl;
                    cerr << "Usage: " << argv[0] << " -p <port>" << endl;
                    return 1;
                }
                break;
            default:
                cerr << "Unknown option: -" << static_cast<char>(optopt) << endl;
                cerr << "Usage: " << argv[0] << " -p <port>" << endl;
                return 1;
        }
    }

    // Check if the port number is provided
    if (port == 0)
    {
        cerr << "A port number must be provided." << endl;
        cerr << "Usage: " << argv[0] << " -p <port>" << endl;
        return 1;
    }

    // Validate if the port number is within the valid range
    if (port < 18000 || port > 18200)
    {
        cerr << "The port number must be between 18000 and 18200 (inclusive)." << endl;
        return 1;
    }

    RFTPReceiver receiver;             
    receiver.initReceiverSocket(port); 

    RFTPPacket packet;                
    
    while (true)
    {   
        struct timeval tv;
        tv.tv_sec = timeout_s;
        tv.tv_usec = timeout_ms * 1000;
        int receiverSocket = receiver.getReceiverSocket();
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(receiverSocket, &fds);
        int activity = select(receiverSocket + 1, &fds, NULL, NULL, &tv);
        if (activity > 0 && FD_ISSET(receiverSocket, &fds)){
            // type of packet is 01000000(0x40)
            if (receiver.receivePacket(packet, 0x40))
            { 
                std::string fileInfo(reinterpret_cast<const char*>(packet.data.data()), packet.data.size());
                size_t pos = fileInfo.rfind('/');

                std::string subdir;
                std::string filename;
                if (pos != std::string::npos) {
                    subdir = fileInfo.substr(0, pos);      
                    filename = fileInfo.substr(pos + 1);   
                    
                    std::cout << "Subdirectory: " << subdir << std::endl;
                    std::cout << "Filename: " << filename << std::endl;
                } else {
                    std::cout << "No directory separator found in the path." << std::endl;
                }

                if (receiver.openFile(subdir, filename + ".recv"))
                {
                for(int i=0; i < infoAckNum; i++){
                    receiver.sendAck(packet.seqNumber, 0x50);
                }

                cerr << "[recv data] 0 (" << packet.data.size() << ") ACCEPTED(in-order)" << endl;
                break;               
              }
              else{
                cout << "openFile failed, please check!!" << endl;
              }
            

            }
        }
 
    }

    uint32_t expectedSeqNumber = 0;       
    std::map<uint32_t, RFTPPacket> bufferMap; 
    bool lastPacketReceived = false;     
    int wincount = 0;
    bool transmissionComplete = false;

    while (!transmissionComplete) {
        struct timeval tv;
        tv.tv_sec = timeout_s;
        tv.tv_usec = timeout_ms * 1000;
        int receiverSocket = receiver.getReceiverSocket();
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(receiverSocket, &fds);
        int activity = select(receiverSocket + 1, &fds, NULL, NULL, &tv);

        if (activity > 0 && FD_ISSET(receiverSocket, &fds)) {
            if (receiver.receivePacket(packet, 0)) {
                // Check if transmission is already complete
                if (lastPacketReceived) {
                    // Ignore any packets after the last packet
                    continue;
                }

                if (packet.seqNumber == expectedSeqNumber) {
                    // Correct packet received
                    receiver.writeFileChunk(packet);
                    cerr << "[recv data] " << packet.seqNumber * maxPayloadSize
                        << " (" << packet.data.size() << ") ACCEPTED(in-order)" << endl;
                    expectedSeqNumber++;
                    wincount++;
                    //cout << "wincount:" << wincount << endl;
                    if (wincount == maxwindowsize) {
                        // Window size reached, send ACK
                        if (expectedSeqNumber != 0) {
                        receiver.sendAck(expectedSeqNumber - 1, 0x10);
                        //cout << "[send ack] " << expectedSeqNumber - 1 << endl;
                    }
                        wincount = 0;
                    }
                } else {
                    // Packet loss detected
                    cerr << "[recv data] " << packet.seqNumber * maxPayloadSize << " "
                        << packet.seqNumber * maxPayloadSize << " (" << packet.data.size() << ") IGNORED" << endl;
                    // Send ACK for the last correctly received packet
                    if (expectedSeqNumber != 0) {
                        receiver.sendAck(expectedSeqNumber - 1, 0x10);
                        //cout << "[send ack] " << expectedSeqNumber - 1 << endl;
                    }
                    wincount = 0;
                    continue;
                }

                // Check if it's the last packet
                if (packet.flags & 0x04) {
                    lastPacketReceived = true;
                    cout << "[completed]" << endl;

                    // Send final ACK for the last packet
                    if (expectedSeqNumber != 0) {
                        for(int i=0; i < lastAckNum; i++) {
                            receiver.sendAck(expectedSeqNumber - 1,0x10);
                        }
                        //cout << "[send ack] " << expectedSeqNumber - 1 << endl;
                    }
                    transmissionComplete = true;
                    break;
                }
            } else {
                // Corrupted packet detected
                cerr << "[recv corrupt packet]" << endl;
                // Send ACK for the last correctly received packet
                if (expectedSeqNumber != 0) {
                    receiver.sendAck(expectedSeqNumber - 1, 0x10);
                    //cout << "[send ack] " << expectedSeqNumber - 1 << endl;
                }
                wincount = 0;
                continue;
            }
        } else {
            // Timeout occurred, resend ACK for the last correctly received packet
            if (expectedSeqNumber != 0) {
                receiver.sendAck(expectedSeqNumber - 1, 0x10);
                //cout << "[send ack] " << expectedSeqNumber - 1 << " (timeout)" << endl;
            }
            //else
                //cout << " Not appliable ack, wait for sender timeout " << endl;
        }
    }

    receiver.closeFile();        
    receiver.printStatistics();   
    receiver.closeReceiverSocket();
    return 0;
}             