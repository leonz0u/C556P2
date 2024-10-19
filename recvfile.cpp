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

// RFTP 数据包结构体
struct RFTPPacket
{
    uint32_t seqNumber;          // 序列号
    uint32_t ackNumber;          // 确认号
    uint8_t flags;               // 标志位
    uint16_t windowSize;         // 窗口大小
    uint16_t checksum;           // 校验和
    char data[maxPayloadSize];   // 数据载荷
    uint16_t dataLength;         // 数据长度
};

// RFTP 接收端类
class RFTPReceiver
{
private:
    struct sockaddr_in receiverAddress; // 接收端地址结构
    struct sockaddr_in senderAddress;   // 发送端地址结构
    int receiverSocket;                 // 套接字描述符
    std::ofstream file;                 // 文件输出流
    uint32_t totalBytesReceived;        // 总接收字节数
    std::chrono::steady_clock::time_point startTime; // 传输开始时间

public:
    RFTPReceiver();                      // 构造函数
    void initReceiverSocket(int portNumber); // 初始化接收端套接字
    bool openFile(const std::string &subPath, const std::string &filename); // 打开接收文件
    void closeFile();                    // 关闭文件
    bool receivePacket(RFTPPacket &packet); // 接收数据包
    bool receivePacketWithTimeout(RFTPPacket &packet, int timeout_sec); // 接收数据包带超时
    void writeFileChunk(const RFTPPacket &packet); // 写入文件块
    void sendAck(uint32_t ackNumber);    // 发送确认包
    uint16_t calculateChecksum(const RFTPPacket &packet); // 计算校验和
    void printStatistics();              // 打印传输统计信息
};

// 构造函数初始化成员变量
RFTPReceiver::RFTPReceiver() : totalBytesReceived(0)
{
    receiverAddress.sin_family = AF_INET;         // 地址族为 IPv4
    receiverAddress.sin_addr.s_addr = INADDR_ANY; // 监听所有可用接口
    receiverSocket = 0;                           // 初始化套接字描述符为 0
    startTime = std::chrono::steady_clock::now(); // 记录传输开始时间
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

    //**************************************************************************************************//
    // 绑定套接字到接收端地址 应该需要检查 但是一直报错 先注释掉
    /*if (bind(receiverSocket, (struct sockaddr *)&receiverAddress, sizeof(receiverAddress)) < 0)
    {
        cerr << "Error: Binding failed!" << endl;
        exit(1);
    }*/
    //**************************************************************************************************//
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

// 关闭文件流
void RFTPReceiver::closeFile()
{
    if (file.is_open())
    {
        file.close();
    }
}

// 接收一个数据包
bool RFTPReceiver::receivePacket(RFTPPacket &packet)
{
    socklen_t senderLen = sizeof(senderAddress); // 发送端地址长度
    // 接收数据包
    int bytesReceived = recvfrom(receiverSocket, &packet, sizeof(packet), 0, (struct sockaddr *)&senderAddress, &senderLen);
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
        return receivePacket(packet);
    }
}

// 将数据块写入文件
void RFTPReceiver::writeFileChunk(const RFTPPacket &packet)
{
    file.write(packet.data, packet.dataLength); // 写入数据
    totalBytesReceived += packet.dataLength;    // 更新总接收字节数
}

// 发送确认包
void RFTPReceiver::sendAck(uint32_t ackNumber)
{
    RFTPPacket ackPacket;              // 创建一个确认包
    memset(&ackPacket, 0, sizeof(ackPacket)); // 初始化确认包为 0
    ackPacket.ackNumber = ackNumber;   // 设置确认号
    ackPacket.flags = 0x10;            // 设置 ACK 标志位
    ackPacket.windowSize = rwnd;        // 设置窗口大小
    ackPacket.checksum = calculateChecksum(ackPacket); // 计算校验和
    
    // 发送确认包到发送端
    sendto(receiverSocket, &ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&senderAddress, sizeof(senderAddress));
}

// 计算数据包的校验和
uint16_t RFTPReceiver::calculateChecksum(const RFTPPacket &packet)
{
    uint32_t sum = 0;
    sum += packet.seqNumber;         // 累加序列号
    sum += packet.ackNumber;         // 累加确认号
    sum += packet.flags;             // 累加标志位
    sum += packet.windowSize;        // 累加窗口大小
    sum += packet.dataLength;        // 累加数据长度
    // 累加数据载荷
    for(int i = 0; i < packet.dataLength; ++i)
    {
        sum += static_cast<uint8_t>(packet.data[i]);
    }
    // 处理进位
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~sum; // 返回校验和的反码
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

int main(int argc, char *argv[]) {
    int port = 0; // 接收端端口号，初始化为 0

    // 解析命令行参数，仅接受 -p <port>
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

    if (port == 0)
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
        if (receiver.receivePacket(packet))
        {
            // 验证校验和
            uint16_t calculatedChecksum = receiver.calculateChecksum(packet);
            if (calculatedChecksum != packet.checksum)
            {
                cout << "[recv corrupt packet]" << endl; // 打印收到损坏的包
                continue; // 丢弃损坏的包，继续接收下一个包
            }

            // 检查是否为信息包（标志位 0x40）
            if (packet.flags & 0x40)
            {
                // 解析目录名和文件名
                std::string fileInfo(packet.data, packet.dataLength);
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
                receiver.sendAck(packet.seqNumber + 1);
                cout << "[recv data] 0 (" << packet.dataLength << ") ACCEPTED(in-order)" << endl;

                break; // 信息包处理完毕，进入数据传输阶段
            }
            else
            {
                cout << "[recv corrupt packet]" << endl; // 非信息包，打印损坏包信息
            }
        }
    }

    // 数据传输阶段
    uint32_t expectedSeqNumber = 0;         // 期望的序列号
    std::map<uint32_t, RFTPPacket> bufferMap; // 缓冲区，存储滑动窗口内的乱序包
    bool lastPacketReceived = false;        // 标识是否接收到最后一个包

    while (true)
    {
        if (lastPacketReceived)
        {
            // 如果已接收到最后一个包，等待一段时间后退出
            bool timeout = true;
            if (receiver.receivePacketWithTimeout(packet, 2)) // 等待 2 秒
            {
                // 接收到新的包，可能是重传的最后一个包
                uint16_t calculatedChecksum = receiver.calculateChecksum(packet);
                if (calculatedChecksum != packet.checksum)
                {
                    cout << "[recv corrupt packet]" << endl;
                    continue;
                }

                if (!(packet.flags & 0x40))
                {
                    if (packet.seqNumber == expectedSeqNumber)
                    {
                        // 接收到期望的包，写入文件
                        receiver.writeFileChunk(packet);
                        cout << "[recv data] " << packet.seqNumber * maxPayloadSize << " (" << packet.dataLength << ") ACCEPTED(in-order)" << endl;
                        expectedSeqNumber++;

                        // 检查缓冲区中是否有连续的包可以写入
                        while (bufferMap.find(expectedSeqNumber) != bufferMap.end())
                        {
                            RFTPPacket bufferedPacket = bufferMap[expectedSeqNumber];
                            receiver.writeFileChunk(bufferedPacket);
                            cout << "[recv data] " << bufferedPacket.seqNumber * maxPayloadSize << " (" << bufferedPacket.dataLength << ") ACCEPTED(in-order)" << endl;
                            bufferMap.erase(expectedSeqNumber); // 移除已写入的包
                            expectedSeqNumber++;
                        }

                        // 发送累计 ACK
                        receiver.sendAck(expectedSeqNumber);
                    }
                    else if (packet.seqNumber > expectedSeqNumber && packet.seqNumber < expectedSeqNumber + rwnd)
                    {
                        // 接收到的包在滑动窗口内且为乱序包
                        if (bufferMap.find(packet.seqNumber) == bufferMap.end())
                        {
                            bufferMap[packet.seqNumber] = packet; // 存入缓冲区
                            cout << "[recv data] " << packet.seqNumber * maxPayloadSize << " (" << packet.dataLength << ") ACCEPTED(out-of-order)" << endl;
                        }

                        // 发送累计 ACK
                        receiver.sendAck(expectedSeqNumber);
                    }
                    else
                    {
                        // 接收到重复包或超出窗口的包，忽略并重新发送 ACK
                        cout << "[recv data] " << packet.seqNumber * maxPayloadSize << " (" << packet.dataLength << ") IGNORED" << endl;
                        receiver.sendAck(expectedSeqNumber);
                    }

                    // 检查是否为最后一个包（标志位 0x04）
                    if (packet.flags & 0x04)
                    {
                        // 确保所有包都已接收
                        if (bufferMap.empty())
                        {
                            cout << "[completed]" << endl; // 打印完成消息
                            break; // 结束传输
                        }
                    }
                }
            }
            else
            {
                // 超时，认为传输完成
                cout << "[completed]" << endl;
                break; // 结束传输
            }
        }
        else
        {
            if (receiver.receivePacket(packet))
            {
                // 验证校验和
                uint16_t calculatedChecksum = receiver.calculateChecksum(packet);
                if (calculatedChecksum != packet.checksum)
                {
                    cout << "[recv corrupt packet]" << endl; // 打印收到损坏的包
                    continue; // 丢弃损坏的包，继续接收下一个包
                }

                // 检查是否为数据包（非信息包）
                if (!(packet.flags & 0x40))
                {
                    if (packet.seqNumber == expectedSeqNumber)
                    {
                        // 接收到期望的包，写入文件
                        receiver.writeFileChunk(packet);
                        cout << "[recv data] " << packet.seqNumber * maxPayloadSize << " (" << packet.dataLength << ") ACCEPTED(in-order)" << endl;
                        expectedSeqNumber++; // 更新期望序列号

                        // 检查缓冲区中是否有连续的包可以写入
                        while (bufferMap.find(expectedSeqNumber) != bufferMap.end())
                        {
                            RFTPPacket bufferedPacket = bufferMap[expectedSeqNumber];
                            receiver.writeFileChunk(bufferedPacket);
                            cout << "[recv data] " << bufferedPacket.seqNumber * maxPayloadSize << " (" << bufferedPacket.dataLength << ") ACCEPTED(in-order)" << endl;
                            bufferMap.erase(expectedSeqNumber); // 移除已写入的包
                            expectedSeqNumber++;
                        }
                        
                        // 发送累计 ACK
                        receiver.sendAck(expectedSeqNumber);
                    }
                    else if (packet.seqNumber > expectedSeqNumber && packet.seqNumber < expectedSeqNumber + rwnd)
                    {
                        // 接收到的包在滑动窗口内且为乱序包
                        if (bufferMap.find(packet.seqNumber) == bufferMap.end())
                        {
                            bufferMap[packet.seqNumber] = packet; // 存入缓冲区
                            cout << "[recv data] " << packet.seqNumber * maxPayloadSize << " (" << packet.dataLength << ") ACCEPTED(out-of-order)" << endl;
                        }
                        
                        // 发送累计 ACK
                        receiver.sendAck(expectedSeqNumber);
                    }
                    else
                    {
                        // 接收到重复包或超出窗口的包，忽略并重新发送 ACK
                        cout << "[recv data] " << packet.seqNumber * maxPayloadSize << " (" << packet.dataLength << ") IGNORED" << endl;
                        receiver.sendAck(expectedSeqNumber);
                    }

                    // 检查是否为最后一个包（标志位 0x04）
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
                else
                {
                    cout << "[recv corrupt packet]" << endl; // 非数据包，打印损坏包信息
                }
            }
        }

    receiver.closeFile();         // 关闭文件
    receiver.printStatistics();   // 打印传输统计信息
    return 0;                     // 正常退出程序
}