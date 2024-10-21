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
#include <sys/select.h> // 用于实现超时接收

using namespace std;

// 全局变量
const int maxPayloadSize = 1450; // 每个数据包的最大有效载荷大小（字节）
const int rwnd = 8;               // 接收窗口大小
const int cwnd = 8;
const int timeout_s = 1;
const int timeout_ms = 0;
const int maxPacketSize = 1472;
// start of checksum in buffer in bytes
const int checksumOffset = 11;

// RFTP 数据包结构体
struct RFTPPacket
{
    uint32_t seqNumber;          // 序列号
    uint32_t ackNumber;          // 确认号
    uint8_t flags;               // 标志位
    uint16_t windowSize;         // 窗口大小
    uint16_t checksum;           // 校验和
    std::vector<uint8_t> data; // 数据载荷
    // char data[maxPayloadSize];   // 数据载荷
    // uint16_t data.size();         // 数据长度
};

// RFTP 接收端类
class RFTPReceiver
{
private:
    struct sockaddr_in receiverAddress; // 接收端地址结构
    struct sockaddr_in senderAddress;   // 发送端地址结构
    int receiverSocket;                 // 套接字描述符
    std::ofstream file;                 // 文件输出流
    int fileSize;
    std::string subdir;
    std::string filename;    
    uint32_t totalBytesReceived;        // 总接收字节数
    std::chrono::steady_clock::time_point startTime; // 传输开始时间

public:
    RFTPReceiver();                      // 构造函数
    void initReceiverSocket(int portNumber); // 初始化接收端套接字
    // setSenderAddress?
    bool openFile(const std::string &subPath, const std::string &filename); // 打开接收文件
    void closeFile();                    // 关闭文件
    bool receivePacket(RFTPPacket &packet,uint8_t type); // 接收数据包
    bool receivePacketWithTimeout(RFTPPacket &packet, int timeout_sec); // 接收数据包带超时
    void writeFileChunk(const RFTPPacket &packet); // 写入文件块
    void sendAck(uint32_t ackNumber);    // 发送确认包
    uint16_t calculateChecksum(const RFTPPacket &packet); // 计算校验和
    void printStatistics();              // 打印传输统计信息
    int getReceiverSocket();              // 获取接收端套接字描述符
    void closeReceiverSocket();
    void sendInfoAck(uint32_t ackNumber);    // 发送确认包
    uint16_t verifyChecksum(const std::vector<uint8_t> &buffer);
};

// 构造函数初始化成员变量
RFTPReceiver::RFTPReceiver() : totalBytesReceived(0)
{
    receiverAddress.sin_family = AF_INET;         // 地址族为 IPv4
    receiverAddress.sin_addr.s_addr = INADDR_ANY; // 监听所有可用接口
    receiverSocket = 0;                           // 初始化套接字描述符为 0
}

// 初始化接收端套接字并绑定到指定端口
void RFTPReceiver::initReceiverSocket(int portNumber)
{
    // 创建 UDP 套接字
    receiverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (receiverSocket < 0)
    {
        cerr << "Error: The receiver socket could not be opened!" << endl;
        exit(1);
    }
    
    receiverAddress.sin_port = htons(portNumber); // 设置接收端端口号

    if (bind(receiverSocket, (struct sockaddr *)&receiverAddress, sizeof(receiverAddress)) < 0)
    {
        cerr << "Error: Binding failed!" << endl;
        exit(1);
    }
}

// 打开用于写入接收文件的文件流
bool RFTPReceiver::openFile(const std::string &subPath, const std::string &filename)
{
    std::string fullPath = subPath + "/" + filename; // 构建完整的文件路径
    // 如果目录不存在，创建目录
    std::string mkdirCommand = "mkdir -p " + subPath;
    system(mkdirCommand.c_str());

    file.open(fullPath, std::ios::binary); // 以二进制模式打开文件
    if (!file.is_open())
    {
        cerr << "Error: Unable to open file for writing!" << endl;
        return false;
    }
    return true;
}

RFTPPacket deserializeRFTPPacket(const std::vector<uint8_t>& buffer) {
    RFTPPacket packet;
    size_t offset = 0;

    // 1. 反序列化 seqNumber
    std::memcpy(&packet.seqNumber, buffer.data() + offset, sizeof(packet.seqNumber));
    offset += sizeof(packet.seqNumber);

    // 2. 反序列化 ackNumber
    std::memcpy(&packet.ackNumber, buffer.data() + offset, sizeof(packet.ackNumber));
    offset += sizeof(packet.ackNumber);

    // 3. 反序列化 flags
    packet.flags = buffer[offset];
    offset += sizeof(packet.flags);

    // 4. 反序列化 windowSize
    std::memcpy(&packet.windowSize, buffer.data() + offset, sizeof(packet.windowSize));
    offset += sizeof(packet.windowSize);

    // 5. 反序列化 checksum
    std::memcpy(&packet.checksum, buffer.data() + offset, sizeof(packet.checksum));
    offset += sizeof(packet.checksum);

    // 6. 反序列化数据内容
    packet.data.resize(buffer.size() - offset);
    std::memcpy(packet.data.data(), buffer.data() + offset, packet.data.size());

    return packet;
}


// 关闭文件流
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

// 接收一个数据包
bool RFTPReceiver::receivePacket(RFTPPacket &packet, uint8_t type)
{   
    std::vector<uint8_t> buffer(maxPacketSize);

    socklen_t senderLen = sizeof(senderAddress); // 发送端地址长度
    // 接收数据包
    
    
    int bytesReceived = recvfrom(receiverSocket, buffer.data(), maxPacketSize, 0, (struct sockaddr *)&senderAddress, &senderLen);
    
    buffer.resize(bytesReceived);

    // packet = deserializeRFTPPacket(buffer);
    if (verifyChecksum(buffer))
    {
        // 校验成功，反序列化数据包
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

// 接收数据包超时
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
        // 超时
        return false;
    }
    else
    {
        // 接收数据包
        return receivePacket(packet, 0);
    }
}

// 将数据块写入文件
void RFTPReceiver::writeFileChunk(const RFTPPacket &packet)
{
    file.write(reinterpret_cast<const char*>(packet.data.data()), packet.data.size()); // 写入数据
    totalBytesReceived += packet.data.size();    // 更新总接收字节数
}

// 发送确认包
void RFTPReceiver::sendAck(uint32_t ackNumber)
{
    RFTPPacket ackPacket;              // 创建一个确认包
    // memset(&ackPacket, 0, sizeof(ackPacket)); // 初始化确认包为 0
    ackPacket.seqNumber = 0;     // 设置序列号为 0
    ackPacket.ackNumber = ackNumber;   // 设置确认号
    ackPacket.flags = 0x10;            // 设置 ACK 标志位
    ackPacket.windowSize = rwnd;        // 设置窗口大小 
    ackPacket.data.resize(0);             // 清空数据部分
    ackPacket.checksum = calculateChecksum(ackPacket); // 计算校验和

    // 发送确认包到发送端
    sendto(receiverSocket, &ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&senderAddress, sizeof(senderAddress));
}

// 发送确认包
void RFTPReceiver::sendInfoAck(uint32_t ackNumber)
{
    RFTPPacket ackPacket;              // 创建一个确认包
    // memset(&ackPacket, 0, sizeof(ackPacket)); // 初始化确认包为 0
    ackPacket.seqNumber = 0;     // 设置序列号为 0
    ackPacket.ackNumber = ackNumber;   // 设置确认号
    ackPacket.flags = 0x50;            // 设置 ACK 标志位
    ackPacket.windowSize = rwnd;        // 设置窗口大小 
    ackPacket.data.resize(0);             // 清空数据部分
    ackPacket.checksum = calculateChecksum(ackPacket); // 计算校验和
    startTime = std::chrono::steady_clock::now(); // 记录传输开始时间

    // 发送确认包到发送端
    sendto(receiverSocket, &ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&senderAddress, sizeof(senderAddress));
}

// 计算数据包的校验和
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
    // 提取接收到的数据包中的 checksum
    uint16_t receivedChecksum = 0;
    memcpy(&receivedChecksum, buffer.data() + checksumOffset, sizeof(uint16_t));

    uint64_t sumX = 0;
    const size_t windowSizeOffset = 9; // 请根据实际情况填写
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

    // // 1. 累加 seqNumber
    // const size_t seqNumberOffset = 0; // 请根据实际情况填写

    // sum += (buffer[seqNumberOffset] << 8) + buffer[seqNumberOffset + 1];
    // sum += (buffer[seqNumberOffset + 2] << 8) + buffer[seqNumberOffset + 3];

    // // 2. 累加 ackNumber
    // const size_t ackNumberOffset = 4; // 请根据 实际情况填写
    // sum += (buffer[ackNumberOffset] << 8) + buffer[ackNumberOffset + 1];
    // sum += (buffer[ackNumberOffset + 2] << 8) + buffer[ackNumberOffset + 3];

    // // 3. 累加 flags
    // const size_t flagsOffset = 8; // 请根据实际情况填写
    // sum += buffer[flagsOffset];

    // // 4. 累加 windowSize
    // const size_t windowSizeOffset = 9; // 请根据实际情况填写
    // sum += (buffer[windowSizeOffset] << 8) + buffer[windowSizeOffset + 1];

    // // 5. 累加数据部分
    // const size_t dataOffset = 13; // 请根据实际情况填写
    // int bufferSize = buffer.size();
    // for (size_t i = dataOffset; i < bufferSize; i += 2)
    // {
    //     uint16_t data = buffer[i];
    //     if (i + 1 < bufferSize)
    //     {
    //         data = (data << 8) + buffer[i + 1];
    //     }
    //     sum += data;
    // }

    
    // 合并高位和低位
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // 计算校验和
    // uint16_t calculatedChecksum = ~sum & 0xFFFF;

    // 返回是否校验通过：1 表示成功，0 表示失败
    return (sum + receivedChecksum == 0xFFFF);
}


// 打印传输统计信息
void RFTPReceiver::printStatistics()
{
    auto endTime = std::chrono::steady_clock::now(); // 记录传输结束时间
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime); // 计算传输时长
    double durationSec = duration.count() / 1000.0; // 将时长转换为秒

    cout << fixed << setprecision(2);
    cout << "Total bytes received: " << totalBytesReceived << " bytes" << endl;
    cout << "Transfer time: " << durationSec << " seconds" << endl;
    cout << "Throughput: " << (totalBytesReceived * 8.0 / 1000000.0) / durationSec << " Mbps" << endl;
}

// getReceiverSocket
int RFTPReceiver::getReceiverSocket()
{
    return receiverSocket; // 返回接收端套接字s
}

int main(int argc, char *argv[]) {
    int port = 18150; // 接收端端口号，初始化为 0

    //解析命令行参数，仅接受 -p <port>
    int opt;
    while ((opt = getopt(argc, argv, "p:")) != -1)
    {
        switch (opt)
        {
            case 'p':
                port = atoi(optarg); // 将端口号字符串转换为整数
                break;
            default:
                cerr << "Usage: " << argv[0] << " -p <port>" << endl;
                return 1;
        }
    }
    if (port == -1)
    {
        cerr << "Usage: " << argv[0] << " -p <port>" << endl;
        return 1;
    }

    RFTPReceiver receiver;              // 创建接收端对象
    receiver.initReceiverSocket(port);  // 初始化接收套接字

    RFTPPacket packet;                  // 创建数据包对象
    
    // 接收信息包阶段
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
                // 验证校验和
                // uint16_t calculatedChecksum = receiver.calculateChecksum(packet);
                // if (calculatedChecksum != packet.checksum)
                // {
                //     cout << "[recv corrupt packet]" << endl; // 打印收到损坏的包
                //     continue; // 丢弃损坏的包，继续接收下一个包
                // }

                
                // 解析目录名和文件名
                std::string fileInfo(reinterpret_cast<const char*>(packet.data.data()), packet.data.size());
                size_t slash_pos = fileInfo.find('/');
                if (slash_pos == std::string::npos)
                {
                    cerr << "Invalid file information format. Expected <subdir>/<filename>" << endl;
                    continue; // 格式错误，继续接收下一个包
                }
                std::string subdir = fileInfo.substr(0, slash_pos);           // 提取子目录名
                std::string filename = fileInfo.substr(slash_pos + 1);         // 提取文件名

                // 打开文件进行写入，添加 ".recv" 后缀
                if (!receiver.openFile(subdir, filename + ".recv"))
                {
                    return 1; // 文件打开失败，退出程序
                }

                // 发送 ACK 确认信息包

                for(int i=0; i<5; i++){
                    receiver.sendInfoAck(packet.seqNumber);

                }
                cout << "[recv data] 0 (" << packet.data.size() << ") ACCEPTED(in-order)" << endl;

                break; // 信息包处理完毕，进入数据传输阶段
            

            }
        }
 
    }

    // 数据传输阶段
    uint32_t expectedSeqNumber = 0;         // 期望的序列号
    std::map<uint32_t, RFTPPacket> bufferMap; // 缓冲区，存储滑动窗口内的乱序包
    bool lastPacketReceived = false;        // 标识是否接收到最后一个包
    const int maxwindowsize = 8;
    int wincount = 0;
    bool transmissionComplete = false;

    /*while (true)
    {
        if (lastPacketReceived)
        {
            // 如果已接收到最后一个包，等待一段时间后退出
            // bool timeout = true;
            if (receiver.receivePacketWithTimeout(packet, 2)) // 等待 2 秒
            {
                receiver.sendAck(packet.seqNumber);
                break;
            }
            // {
            //     // 接收到新的包，可能是重传的最后一个包
            //     uint16_t calculatedChecksum = receiver.calculateChecksum(packet);
            //     if (calculatedChecksum != packet.checksum)
            //     {
            //         cout << "[recv corrupt packet]" << endl;
            //         continue;
            //     }

            //     if (!(packet.flags & 0x40))
            //     {
            //         if (packet.seqNumber == expectedSeqNumber)
            //         {
            //             // 接收到期望的包，写入文件
            //             receiver.writeFileChunk(packet);
            //             cout << "[recv data] " << packet.seqNumber * maxPayloadSize << " (" << packet.data.size() << ") ACCEPTED(in-order)" << endl;
            //             expectedSeqNumber++;

            //             // 检查缓冲区中是否有连续的包可以写入
            //             while (bufferMap.find(expectedSeqNumber) != bufferMap.end())
            //             {
            //                 RFTPPacket bufferedPacket = bufferMap[expectedSeqNumber];
            //                 receiver.writeFileChunk(bufferedPacket);
            //                 cout << "[recv data] " << bufferedPacket.seqNumber * maxPayloadSize << " (" << bufferedPacket.data.size() << ") ACCEPTED(in-order)" << endl;
            //                 bufferMap.erase(expectedSeqNumber); // 移除已写入的包
            //                 expectedSeqNumber++;
            //             }

            //             // 发送累计 ACK
            //             receiver.sendAck(expectedSeqNumber-1);
            //         }
            //         else if (packet.seqNumber > expectedSeqNumber && packet.seqNumber < expectedSeqNumber + rwnd)
            //         {
            //             // 接收到的包在滑动窗口内且为乱序包
            //             if (bufferMap.find(packet.seqNumber) == bufferMap.end())
            //             {
            //                 bufferMap[packet.seqNumber] = packet; // 存入缓冲区
            //                 cout << "[recv data] " << packet.seqNumber * maxPayloadSize << " (" << packet.data.size() << ") ACCEPTED(out-of-order)" << endl;
            //             }

            //             // 发送累计 ACK
            //             receiver.sendAck(expectedSeqNumber-1);
            //         }
            //         else
            //         {
            //             // 接收到重复包或超出窗口的包，忽略并重新发送 ACK
            //             cout << "[recv data] " << packet.seqNumber * maxPayloadSize << " (" << packet.data.size() << ") IGNORED" << endl;
            //             receiver.sendAck(expectedSeqNumber-1);
            //         }

            //         // 检查是否为最后一个包（标志位 0x04）
            //         if (packet.flags & 0x04)
            //         {
            //             // 确保所有包都已接收
            //             if (bufferMap.empty())
            //             {
            //                 cout << "[completed]" << endl; // 打印完成消息
            //                 break; // 结束传输
            //             }
            //         }
            //     }
            // }
            else
            {
                // 超时，认为传输完成
                cout << "[completed]" << endl;
                break; // 结束传输
            }
        }
        else
        {
        struct timeval tv;
        tv.tv_sec = timeout_s;
        tv.tv_usec = timeout_ms * 1000;
        int receiverSocket = receiver.getReceiverSocket();
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(receiverSocket, &fds);
        int activity = select(receiverSocket + 1, &fds, NULL, NULL, &tv);
        //没满8 写入缓冲区 计数器 + 1 continue
        //满8 计数器清0 发ack
        if (activity > 0 && FD_ISSET(receiverSocket, &fds)){
            
            if (receiver.receivePacket(packet, 0))
            {
                // 验证校验和
                // if (packet.seqNumber == 7){
                //     int x = 7;
                // }
                // uint16_t calculatedChecksum = receiver.calculateChecksum(packet);


                // 检查是否为数据包（非信息包）
                // have two type of data packet flag
                // 00000000 and 00000100
                // if (!(packet.flags))
                
                    if (packet.seqNumber == expectedSeqNumber)
                    {
                        // 接收到期望的包，写入文件
                        receiver.writeFileChunk(packet);
                        cout << "[recv data] " << packet.seqNumber * maxPayloadSize << " (" << packet.data.size() << ") ACCEPTED(in-order)" << endl;
                        expectedSeqNumber++; // 更新期望序列号

                        // 检查缓冲区中是否有连续的包可以写入
                        while (bufferMap.find(expectedSeqNumber) != bufferMap.end())
                        {
                            RFTPPacket bufferedPacket = bufferMap[expectedSeqNumber];
                            receiver.writeFileChunk(bufferedPacket);
                            cout << "[recv data] " << bufferedPacket.seqNumber * maxPayloadSize << " (" << bufferedPacket.data.size() << ") ACCEPTED(in-order)" << endl;
                            bufferMap.erase(expectedSeqNumber); // 移除已写入的包
                            expectedSeqNumber++;
                        }
                        
                        // 发送累计 ACK
                        receiver.sendAck(expectedSeqNumber-1);
                    }
                    else if (packet.seqNumber > expectedSeqNumber && packet.seqNumber < expectedSeqNumber + rwnd)
                    {
                        // 接收到的包在滑动窗口内且为乱序包
                        if (bufferMap.find(packet.seqNumber) == bufferMap.end())
                        {
                            bufferMap[packet.seqNumber] = packet; // 存入缓冲区
                            cout << "[recv data] " << packet.seqNumber * maxPayloadSize << " (" << packet.data.size() << ") ACCEPTED(out-of-order)" << endl;
                        }
                        
                        // 发送累计 ACK,
                        receiver.sendAck(expectedSeqNumber-1);
                    }
                    else
                    {
                        // 接收到重复包或超出窗口的包，忽略并重新发送 ACK
                        cout << "[recv data] " << packet.seqNumber << " " << packet.seqNumber * maxPayloadSize << " (" << packet.data.size() << ") IGNORED" << endl;
                        cout << "ack number" << expectedSeqNumber-1 << endl;
                        receiver.sendAck(expectedSeqNumber-1);
                    }

                    // 检查是否为最后一个包（标志位 0x04）
                    // if (packet.seqNumber == 7){
                    //     continue;
                    // }
                    if (packet.flags & 0x04)
                    {
                        // 确保所有包都已接收
                        if (bufferMap.empty())
                        {
                            // 标记已接收到最后一个包
                            lastPacketReceived = true;
                            cout << "[completed]" << endl; // 打印完成消息
                            // 不立即退出，而是进入等待状态
                        }
                    }
                

            }
            else{
                // courrupt packet
                cerr<<"corrupt packet, receivepackage failed"<<endl;
            }
        }
        }
    }*/
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
                    cout << "[recv data] " << packet.seqNumber
                        << " (" << packet.data.size() << ") ACCEPTED(in-order)" << endl;
                    expectedSeqNumber++;
                    wincount++;
                    cout << "wincount:" << wincount << endl;
                    if (wincount == maxwindowsize) {
                        // Window size reached, send ACK
                        receiver.sendAck(expectedSeqNumber - 1);
                        cout << "[send ack] " << expectedSeqNumber - 1 << endl;
                        wincount = 0;
                    }
                } else {
                    // Packet loss detected
                    cout << "[recv data] " << packet.seqNumber << " "
                        << packet.seqNumber * maxPayloadSize << " (" << packet.data.size() << ") IGNORED" << endl;
                    // Send ACK for the last correctly received packet
                    receiver.sendAck(expectedSeqNumber - 1);
                    cout << "[send ack] " << expectedSeqNumber - 1 << endl;
                    wincount = 0;
                    continue;
                }

                // Check if it's the last packet
                if (packet.flags & 0x04) {
                    lastPacketReceived = true;
                    cout << "[completed]" << endl;
                    // Send final ACK for the last packet
                    receiver.sendAck(expectedSeqNumber - 1);
                    cout << "[send ack] " << expectedSeqNumber - 1 << endl;
                    transmissionComplete = true;
                    break;
                }
            } else {
                // Corrupted packet detected
                cerr << "Corrupt packet, receivePacket failed" << endl;
                // Send ACK for the last correctly received packet
                receiver.sendAck(expectedSeqNumber - 1);
                cout << "[send ack] " << expectedSeqNumber - 1 << endl;
                wincount = 0;
                continue;
            }
        } else {
            // Timeout occurred, resend ACK for the last correctly received packet
            receiver.sendAck(expectedSeqNumber - 1);
            cout << "[send ack] " << expectedSeqNumber - 1 << " (timeout)" << endl;
        }
    }

    receiver.closeFile();         // 关闭文件
    receiver.printStatistics();   // 打印传输统计信息
    receiver.closeReceiverSocket();
    return 0;                     // 正常退出程序
}