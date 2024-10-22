#!/bin/bash
# Target receiver's IP address and port
RECV_IP="128.42.124.187"
RECV_PORT="18105"

# Directory for sending files (script's relative path is send directory)
cd /local/zx55comp429/project2/send

# List of netsim parameter combinations
NETSIM_PARAMS=(
  "--delay 40 --drop 25 --reorder 10 --mangle 5 --duplicate 15" # delay: 40%, drop: 25%, reorder: 10%, mangle: 5%, duplicate: 15%
  "--delay 45 --drop 30 --reorder 20 --mangle 10 --duplicate 25" # delay: 45%, drop: 30%, reorder: 20%, mangle: 10%, duplicate: 25%
  "--delay 35 --drop 20 --reorder 30 --mangle 20 --duplicate 20" # delay: 35%, drop: 20%, reorder: 30%, mangle: 20%, duplicate: 20%
  "--delay 25 --drop 40 --reorder 25 --mangle 15 --duplicate 30" # delay: 25%, drop: 40%, reorder: 25%, mangle: 15%, duplicate: 30%
  "--delay 30 --drop 45 --reorder 40 --mangle 25 --duplicate 35" # delay: 30%, drop: 45%, reorder: 40%, mangle: 25%, duplicate: 35%
  "--delay 20 --drop 35 --reorder 50 --mangle 30 --duplicate 40" # delay: 20%, drop: 35%, reorder: 50%, mangle: 30%, duplicate: 40%
  "--delay 10 --drop 30 --reorder 60 --mangle 35 --duplicate 45" # delay: 10%, drop: 30%, reorder: 60%, mangle: 35%, duplicate: 45%
  "--delay 15 --drop 40 --reorder 70 --mangle 40 --duplicate 50" # delay: 15%, drop: 40%, reorder: 70%, mangle: 40%, duplicate: 50%
  "--delay 5 --drop 20 --reorder 80 --mangle 45 --duplicate 55" # delay: 5%, drop: 20%, reorder: 80%, mangle: 45%, duplicate: 55%
  "--delay 12 --drop 25 --reorder 90 --mangle 50 --duplicate 60" # delay: 12%, drop: 25%, reorder: 90%, mangle: 50%, duplicate: 60%
)


# List of files to be tested
FILES=("test_500B.bin" "test_11600B.bin" "test_47000B.bin" "test_24MB.bin" "test_64MB.bin")

# Results output file
RESULT_FILE="../sendfile_results.log"  # Save log in parent directory

echo "Automated Transfer Test Results" > $RESULT_FILE
echo "==============================" >> $RESULT_FILE

# Test each netsim parameter combination and file
for PARAM in "${NETSIM_PARAMS[@]}"; do
    # Configure network simulator
    echo "Running netsim with parameters: $PARAM" | tee -a $RESULT_FILE
    /usr/bin/netsim $PARAM

    # Test each file
    for FILE in "${FILES[@]}"; do
        if [ -f "$FILE" ]; then
            echo "Testing file: $FILE with netsim params: $PARAM" | tee -a $RESULT_FILE
            # Ensure recvfile has restarted and is ready
            echo "Waiting for recvfile to be ready..."
            sleep 3  # Wait 3 seconds to ensure receiver is restarted and ready
            
            # Record test start time
            START_TIME=$(date +%s)
            # Run sendfile program, specifying receiver and file
            ./sendfile -r $RECV_IP:$RECV_PORT -f $FILE 2>&1 | tee -a $RESULT_FILE
            # Record test end time
            END_TIME=$(date +%s)
            
            # Calculate transfer duration
            DURATION=$((END_TIME - START_TIME))
            # Record results
            echo "File: $FILE | Params: $PARAM | Duration: ${DURATION}s" | tee -a $RESULT_FILE
            echo "----------------------------------------" | tee -a $RESULT_FILE
        else
            echo "File $FILE not found in current directory. Skipping..." | tee -a $RESULT_FILE
        fi
    done
    
    # Reset network simulator settings
    /usr/bin/netsim
done

echo "All tests completed. Results saved to $RESULT_FILE."