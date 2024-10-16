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

// Structure for the RFTP packet
struct RFTPPacket
{
    // 32-bit sequence number
    uint32_t seqNumber;
    // 32-bit acknowledgement number
    uint32_t ackNumber;
    // 8-bit flags
    // if bit 4 is set, it is an ACK
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
    struct RFTPPacket sendPacket;
    std::ifstream file;
    int fileSize;
    std::string subdir;
    std::string filename;

public:
    RFTPSender();
    void initSenderSocket();
    void setReceiverAddress(string ipAddress, int portNumber);
    bool openFile(const std::string &subPath, const std::string &filename);
    void closeFile();
    void readFileChunk(RFTPPacket &packet);
    bool sendPacket(RFTPPacket &packet);
    
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

// read the file chunk
void RFTPSender::readFileChunk(RFTPPacket &packet)
{
    file.read(reinterpret_cast<char *>(packet.data.data()), maxPayloadSize);
    packet.data.resize(file.gcount());
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