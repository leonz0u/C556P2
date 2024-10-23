# COMP 556 - Introduction to Computer Networks - Project 2 - CompReliable File Transfer Protocol Implementation
## Group Members
This project is a group work of four MCS students, including Chengxuan Zou(cz76), Haoran Zhang(hz115), Zhenhua Zhang(zz123) and Zilong Xue(zx55).


## Overview
This project implements a reliable file transfer protocol using UDP as the underlying transport protocol. The implementation includes a sliding window mechanism for flow control and various error handling capabilities to ensure reliable data transfer over unreliable networks.

## Project Structure
```
PROJECT2/
├── recv/
│   ├── auto_recv.sh       # Automatic receiver restart script
│   └── recvfile           # Receiver executable
├── send/
│   ├── auto_send.sh       # Automatic file sending script
│   ├── sendfile           # Sender executable
│   ├── test_500B.bin      # Test file (500 bytes)
│   ├── test_11600B.bin    # Test file (11.6 KB)
│   ├── test_47000B.bin    # Test file (47 KB)
│   ├── test_94000B.bin    # Test file (94 KB)
│   ├── test_376000B.bin   # Test file (376 KB)
│   ├── test_24MB.bin      # Test file (24 MB)
│   └── test_64MB.bin      # Test file (64 MB)
├── .gitignore             
├── LICENSE                
├── makefile              # Project build file
├── README.md             # Project documentation
├── recvfile.cpp          # Receiver implementation
└── sendfile.cpp          # Sender implementation
```

## Protocol Design

### Packet Format
Our RFTP (Reliable File Transfer Protocol) packet structure consists of:
- **Sequence Number** (32 bits): Identifies the packet sequence
- **Acknowledgment Number** (32 bits): Indicates the next expected sequence number
- **Flags** (8 bits): Control flags for packet type and status
  - Bit 6 (0x40): File information packet
  - Bit 4 (0x10): ACK packet
  - Bit 2 (0x04): Last data packet
- **Window Size** (16 bits): Current window size for flow control
- **Checksum** (16 bits): For error detection
- **Data Payload**: Variable length data (max 1450 bytes)

### Protocol Features
1. **Sliding Window Implementation**
   - Window Size: 32 packets
   - Dynamic window adjustment based on network conditions
   - Supports both in-order and out-of-order packet processing

2. **Reliability Mechanisms**
   - Checksums for error detection
   - Selective acknowledgment
   - Timeout-based retransmission
   - Duplicate packet detection
   - Packet reordering handling

3. **Flow Control**
   - Receiver window (rwnd) of 32 packets
   - Congestion window (cwnd) of 32 packets
   - Window size adjustment based on network conditions

4. **Error Handling**
   - Corrupted packet detection and rejection
   - Lost packet recovery through retransmission
   - Handling of duplicate packets
   - Recovery from reordered packets


## Building and Running

### Build Instructions
```bash
make clean
make all
```

### Running the Programs

1. Start the receiver:
```bash
./recvfile -p <port_number>
```
Example:
```bash
./recvfile -p 18001
```

2. Start the sender:
```bash
./sendfile -r <receiver_host>:<receiver_port> -f <subdir>/<filename>
```
Example:
```bash
./sendfile -r 127.0.0.1:18000 -f ./test_500B.bin
```

## Performance Testing Results

This testing involved the use of two scripts: `auto_send.sh` for automatically sending files under different `netsim` parameters and another for `auto_recv.sh` for automatically restarting the receiver after each transfer. This part details the results of testing file transfers under different network conditions using `netsim` parameters. Additionally, we conducted a baseline test with no netsim parameters set (all set to 0) to observe the optimal transfer conditions without any artificial network interference. We used five test files of varying sizes, ranging from 500 bytes to 64 MB, and tested them under two distinct sets of network conditions. The purpose of this test was to evaluate the impact of network characteristics such as delay, packet drop rate, reorder, mangle, and duplication on file transfer performance, specifically focusing on transfer time and throughput.

### Test Files

The following files were used in the tests:

- `test_500B.bin` (500 bytes)
- `test_11600B.bin` (11,600 bytes)
- `test_47000B.bin` (47,000 bytes)
- `test_24MB.bin` (24 MB)
- `test_64MB.bin` (64 MB)

### Netsim Parameters

Two sets of `netsim` parameters were used during testing:

1. `--delay 40 --drop 25 --reorder 10 --mangle 5 --duplicate 15`

   - Delay: 40%
   - Drop: 25%
   - Reorder: 10%
   - Mangle: 5%
   - Duplicate: 15%

2. `--delay 45 --drop 30 --reorder 20 --mangle 10 --duplicate 25`

   - Delay: 45%
   - Drop: 30%
   - Reorder: 20%
   - Mangle: 10%
   - Duplicate: 25%

### Results

### Test Results with Default Netsim Parameters (All Set to 0)

| File              | Total Bytes Received | Transfer Time (s) | Throughput (Mbps) |
|-------------------|----------------------|-------------------|-------------------|
| `test_500B.bin`   | 500 bytes            | 0.000368          | 10.869            |
| `test_11600B.bin` | 11,600 bytes         | 0.000675          | 137.481           |
| `test_47000B.bin` | 47,000 bytes         | 0.002214          | 169.82            |
| `test_24MB.bin`   | 24 MB                | 1.21              | 166.94            |
| `test_64MB.bin`   | 64 MB                | 3.03              | 177.24            |

#### Test Results for Netsim Parameters Set 1

| File              | Total Bytes Received | Transfer Time (s) | Throughput (Mbps) |
|-------------------|----------------------|-------------------|-------------------|
| `test_500B.bin`   | 500 bytes            | 0.00              | 2.00              |
| `test_11600B.bin` | 11,600 bytes         | 1.96              | 0.05              |
| `test_47000B.bin` | 47,000 bytes         | 0.12              | 3.16              |
| `test_24MB.bin`   | 24 MB                | 88.40             | 2.28              |
| `test_64MB.bin`   | 64 MB                | 238.57            | 2.25              |

#### Test Results for Netsim Parameters Set 2

| File              | Total Bytes Received | Transfer Time (s) | Throughput (Mbps) |
|-------------------|----------------------|-------------------|-------------------|
| `test_500B.bin`   | 500 bytes            | 2.00              | 0.01              |
| `test_11600B.bin` | 11,600 bytes         | 0.03              | 3.44              |
| `test_47000B.bin` | 47,000 bytes         | 0.11              | 3.39              |
| `test_24MB.bin`   | 24 MB                | 97.71             | 2.06              |
| `test_64MB.bin`   | 64 MB                | 252.56            | 2.13              |
