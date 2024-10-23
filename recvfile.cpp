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

const int maxPayloadSize = 1458;
const int rwnd = 32;              
const int cwnd = 32;
const int timeout_s = 0;
const int timeout_ms = 2;
const int maxPacketSize = 1472;
const int checksumOffset = 11;
const int infoAckNum = 4;
const int lastAckNum = 4;
const int maxwindowsize = 32;
// Size of Packet Field
const int seqNum_Size = sizeof(uint32_t);
const int ackNum_Size = sizeof(uint32_t);
const int flags_Size = sizeof(uint8_t);
const int windowSize_Size = sizeof(uint16_t);
const int checksum_Size = sizeof(uint16_t);

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
    // void writeFileChunk(const RFTPPacket &packet);
    void writeFileChunk(const uint8_t *data, int size);
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

    // Set the receive buffer size
    int recvBufferSize = 1048576; 
    if (setsockopt(receiverSocket, SOL_SOCKET, SO_RCVBUF, &recvBufferSize, sizeof(recvBufferSize)) < 0)
    {
        std::cerr << "Error: Could not set socket receive buffer size!" << std::endl;
        close(receiverSocket);
        exit(1);
    }
}

// clear the socket buffer
void RFTPReceiver::clearSocketBuffer(int socket)
{
    char buffer[1472];
    while (recvfrom(socket, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr *)&senderAddress, (socklen_t *)&senderAddress) > 0)
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
    std::string file_name = filename;
    std::string fullPath = "./" + subPath + "/" + file_name + ".recv"; 
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

    std::memcpy(&packet.seqNumber, buffer.data() + offset, seqNum_Size);
    offset += seqNum_Size;
    std::memcpy(&packet.ackNumber, buffer.data() + offset, ackNum_Size);
    offset += ackNum_Size;

    packet.flags = buffer[offset];
    offset += flags_Size;

    std::memcpy(&packet.windowSize, buffer.data() + offset, windowSize_Size);
    offset += windowSize_Size;

    std::memcpy(&packet.checksum, buffer.data() + offset, checksum_Size);
    offset += checksum_Size;

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
            // std::cerr << "Packet type mismatch!, but checksum is correct" << std::endl;
            return false;
        }
        
    }
    else
    {
        // std::cerr << "Checksum mismatch, packet corrupted!" << std::endl;
        return false;
    }

    if (bytesReceived < 0)
    {
        // cerr << "Error: Receiving Packet failed!" << endl;
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


void RFTPReceiver::writeFileChunk(const uint8_t *data, int size)
{
    file.write(reinterpret_cast<const char*>(data), size);
    totalBytesReceived += size; 
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
    std::memcpy(buffer.data() + offset, &ackPacket.seqNumber, seqNum_Size);
    offset += seqNum_Size;
    // Add ackNumber to buffer
    std::memcpy(buffer.data() + offset, &ackPacket.ackNumber, ackNum_Size);
    offset += ackNum_Size;
    // Add flags to buffer
    std::memcpy(buffer.data() + offset, &ackPacket.flags, flags_Size);
    offset += flags_Size;
    // Add windowSize to buffer
    std::memcpy(buffer.data() + offset, &ackPacket.windowSize, windowSize_Size);
    offset += windowSize_Size;
    // Add checksum to buffer
    std::memcpy(buffer.data() + offset, &ackPacket.checksum, checksum_Size);

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
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime); // 使用微秒以获得更高精度
    // cout duration in microseconds
    cout << "Duration: " << duration.count() << " microseconds" << endl;
    
    // cout duration in seconds with protection against zero
    double durationSec = std::max(duration.count() / 1000000.0, 0.000001); // 确保最小1微秒
    
    cout << fixed << setprecision(6); // 增加到6位小数
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

    
    while (true)
    {   
        struct timeval tv;
        tv.tv_sec = timeout_s;
        tv.tv_usec = timeout_ms * 1000;
        int receiverSocket = receiver.getReceiverSocket();
        RFTPPacket packet;                
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
                filename = filename.substr(0, filename.find('\0'));
                if (receiver.openFile(subdir, filename))
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
    bool lastPacketReceived = false;     
    int wincount = 0;
    bool transmissionComplete = false;
    // std::vector<RFTPPacket> receiveBuffer(maxwindowsize);                // Buffer to store received packets
    std::vector<std::vector<uint8_t>> receiveBuffer(maxwindowsize);                // Buffer to store received packets
    // initialize the buffer
    for (int i = 0; i < maxwindowsize; i++)
    {
        receiveBuffer[i].resize(maxPayloadSize);
    }

    while (!transmissionComplete) {
        RFTPPacket packet;
        // receive packet buffer
        struct timeval tv;
        tv.tv_sec = timeout_s;
        tv.tv_usec = timeout_ms * 1000;
        int receiverSocket = receiver.getReceiverSocket();
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(receiverSocket, &fds);
        int activity = select(receiverSocket + 1, &fds, NULL, NULL, &tv);

        if (activity > 0 && FD_ISSET(receiverSocket, &fds))
        {
            if (receiver.receivePacket(packet, 0))
            {
                // Check if transmission is already complete
                if (lastPacketReceived)
                {
                    // Ignore any packets after the last packet
                    continue;
                }

                if (packet.seqNumber < expectedSeqNumber + maxwindowsize)
                {
                    // Correct packet received
                    if (packet.seqNumber == expectedSeqNumber)
                    {
                        expectedSeqNumber++;
                        wincount++;
                        // save the packet data to the buffer
                        int packetDataSize = packet.data.size();
                        receiveBuffer[wincount - 1].resize(packetDataSize);

                        // memcpy(receiveBuffer[wincount - 1].data.data(), packet.data.data(), packetDataSize);
                        memcpy(receiveBuffer[wincount - 1].data(), packet.data.data(), packetDataSize);
                        // receiveBuffer[wincount - 1].data.data() = packet.data.data();
                        cerr << "[recv data] " << packet.seqNumber * maxPayloadSize
                             << " (" << packetDataSize << ") ACCEPTED(in-order)" << endl;
                        // cout << "wincount:" << wincount << endl;
                        if (wincount == maxwindowsize)
                        {
                            if (expectedSeqNumber != 0)
                            {
                                // Window size reached, send ACK
                                receiver.sendAck(expectedSeqNumber - 1, 0x10);
                            }
                            // Write the buffer to file if the window is full
                            for (int i = 0; i < wincount; i++)
                            {
                                receiver.writeFileChunk(receiveBuffer[i].data(), maxPayloadSize);
                            }

                            wincount = 0;
                        }

                        // Check if it's the last packet
                        if (packet.flags & 0x04)
                        {
                            lastPacketReceived = true;
                            cout << "[completed]" << endl;

                            // Send final ACK for the last packet
                            if (expectedSeqNumber != 0)
                            {
                                for (int i = 0; i < lastAckNum; i++)
                                {
                                    receiver.sendAck(expectedSeqNumber - 1, 0x10);
                                }
                                // cout << "[send ack] " << expectedSeqNumber - 1 << endl;
                            }

                            // Write the buffer to file if the window is not full
                            for (int i = 0; i < wincount; i++)
                            {
                                receiver.writeFileChunk(receiveBuffer[i].data(), receiveBuffer[i].size());
                            }
                            transmissionComplete = true;
                            break;
                        }
                    }
                    // Packet in the window but not the expected one
                    else
                    {
                        cerr << "[recv data] " << packet.seqNumber * maxPayloadSize
                             << " (" << packet.data.size() << ") ACCEPTED(out-of-order)" << endl;
                        // Send ACK for the last correctly received packet
                        if (expectedSeqNumber != 0)
                        {
                            receiver.sendAck(expectedSeqNumber - 1, 0x10);
                            // cout << "[send ack] " << expectedSeqNumber - 1 << endl;
                        }
                    }
                }
                else
                {
                    // Packet loss detected
                    cerr << "[recv data] " << packet.seqNumber * maxPayloadSize << " "
                        << packet.seqNumber * maxPayloadSize << " (" << packet.data.size() << ") IGNORED" << endl;
                    // Send ACK for the last correctly received packet
                    // if (expectedSeqNumber != 0) {
                    //     receiver.sendAck(expectedSeqNumber - 1, 0x10);
                    //     //cout << "[send ack] " << expectedSeqNumber - 1 << endl;
                    // }
                    // wincount = 0;
                    continue;
                }
            }
            else
            {
                // Corrupted packet detected
                cerr << "[recv corrupt packet]" << endl;
                // Send ACK for the last correctly received packet
                if (expectedSeqNumber != 0) {
                    // receiver.sendAck(expectedSeqNumber - 1, 0x10);
                    //cout << "[send ack] " << expectedSeqNumber - 1 << endl;
                }
                // wincount = 0;
                continue;
            }
        }
        else
        {
            // Timeout occurred, resend ACK for the last correctly received packet
            if (expectedSeqNumber != 0) {
                receiver.sendAck(expectedSeqNumber - 1, 0x10);
                //cout << "[send ack] " << expectedSeqNumber - 1 << " (timeout)" << endl;
            }
            //else
                //cout << " Not appliable ack, wait for sender timeout " << endl;
        }
    }

    receiver.printStatistics();   
    receiver.closeFile();        
    receiver.closeReceiverSocket();
    return 0;
}             