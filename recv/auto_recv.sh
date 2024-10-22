#!/bin/bash

# Set receiving port
RECV_PORT="18105"

# File path configuration
SEND_DIR="/storage-home/z/zx55/project2/send"
RECV_DIR="/storage-home/z/zx55/project2/recv"
MD5_LOG_FILE="${RECV_DIR}/md5sum_results.log"

# Infinite loop to automatically restart recvfile
while true; do
    # Create a unique log file for each transfer using timestamp
    TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
    LOG_FILE="${RECV_DIR}/recvfile_${TIMESTAMP}.log"

    # Run recvfile and save output to log file
    echo "Starting recvfile on port $RECV_PORT..." | tee -a $LOG_FILE
    ${RECV_DIR}/recvfile -p $RECV_PORT 2>&1 | tee -a $LOG_FILE

    # Check recvfile exit status
    if [ $? -eq 0 ]; then
        echo "recvfile exited normally. Restarting to wait for next file..." | tee -a $LOG_FILE

        # Find the most recently received file (assuming filename starts with test_ and ends with .bin)
        RECEIVED_FILE=$(ls ${RECV_DIR}/test_*.bin | tail -n 1)
        if [ -f "$RECEIVED_FILE" ]; then
            ORIGINAL_FILE="${SEND_DIR}/$(basename $RECEIVED_FILE)"  # Get path of original file

            # Check if original file exists and perform MD5 verification
            if [ -f "$ORIGINAL_FILE" ]; then
                md5_original=$(md5sum "$ORIGINAL_FILE" | awk '{print $1}')
                md5_received=$(md5sum "$RECEIVED_FILE" | awk '{print $1}')

                if [ "$md5_original" == "$md5_received" ]; then
                    echo "[$(date +"%Y-%m-%d %H:%M:%S")] File $(basename $ORIGINAL_FILE) transferred successfully. MD5 checks match." | tee -a $MD5_LOG_FILE
                else
                    echo "[$(date +"%Y-%m-%d %H:%M:%S")] File $(basename $ORIGINAL_FILE) failed to transfer correctly. MD5 checks do not match." | tee -a $MD5_LOG_FILE
                fi
            else
                echo "[$(date +"%Y-%m-%d %H:%M:%S")] Original file $ORIGINAL_FILE not found for MD5 comparison." | tee -a $MD5_LOG_FILE
            fi

            # Delete received file
            rm -f "$RECEIVED_FILE"
            echo "[$(date +"%Y-%m-%d %H:%M:%S")] Deleted received file: $RECEIVED_FILE" | tee -a $LOG_FILE
        else
            echo "No received file found for MD5 comparison." | tee -a $LOG_FILE
        fi
    else
        echo "recvfile encountered an error. Restarting..." | tee -a $LOG_FILE
    fi

    # Brief pause to avoid rapid consecutive restarts
    sleep 1
done