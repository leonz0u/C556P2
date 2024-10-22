# C556P2
Project 2 For COMP 556

# Netsim Parameters Testing Results

## Overview

This testing involved the use of two scripts: `auto_send.sh` for automatically sending files under different `netsim` parameters and another for `auto_recv.sh` for automatically restarting the receiver after each transfer. This part details the results of testing file transfers under different network conditions using `netsim` parameters. We used five test files of varying sizes, ranging from 500 bytes to 64 MB, and tested them under two distinct sets of network conditions. The purpose of this test was to evaluate the impact of network characteristics such as delay, packet drop rate, reorder, mangle, and duplication on file transfer performance, specifically focusing on transfer time and throughput.

## Test Files

The following files were used in the tests:

- `test_500B.bin` (500 bytes)
- `test_11600B.bin` (11,600 bytes)
- `test_47000B.bin` (47,000 bytes)
- `test_24MB.bin` (24 MB)
- `test_64MB.bin` (64 MB)

## Netsim Parameters

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

## Results

### Test Results for Netsim Parameters Set 1

| File              | Total Bytes Received | Transfer Time (s) | Throughput (Mbps) |
|-------------------|----------------------|-------------------|-------------------|
| `test_500B.bin`   | 500 bytes            | 0.00              | 2.00              |
| `test_11600B.bin` | 11,600 bytes         | 1.96              | 0.05              |
| `test_47000B.bin` | 47,000 bytes         | 0.12              | 3.16              |
| `test_24MB.bin`   | 24 MB                | 88.40             | 2.28              |
| `test_64MB.bin`   | 64 MB                | 238.57            | 2.25              |

### Test Results for Netsim Parameters Set 2

| File              | Total Bytes Received | Transfer Time (s) | Throughput (Mbps) |
|-------------------|----------------------|-------------------|-------------------|
| `test_500B.bin`   | 500 bytes            | 2.00              | 0.01              |
| `test_11600B.bin` | 11,600 bytes         | 0.03              | 3.44              |
| `test_47000B.bin` | 47,000 bytes         | 0.11              | 3.39              |
| `test_24MB.bin`   | 24 MB                | 97.71             | 2.06              |
| `test_64MB.bin`   | 64 MB                | 252.56            | 2.13              |

