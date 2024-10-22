CC = g++
#for debug
# CFLAGS = -g -Wall
# for release
CFLAGS = -O2 -Wall

all: recvfile sendfile

sendfile: sendfile.cpp
	mkdir -p send
	$(CC) $(CFLAGS) -o ./send/sendfile sendfile.cpp

recvfile: recvfile.cpp
	mkdir -p recv
	$(CC) $(CFLAGS) -o ./recv/recvfile recvfile.cpp

clean:
	rm -f ./sendfile ./recvfile ./send/sendfile ./recv/recvfile