CC = g++
#for debug
CFLAGS = -g -Wall
#for release
#CFLAGS = -O2 -Wall

all: recvfile sendfile

sendfile: sendfile.cpp
	$(CC) $(CFLAGS) -o ./send/sendfile sendfile.cpp

recvfile: recvfile.cpp
	$(CC) $(CFLAGS) -o ./recv/recvfile recvfile.cpp

clean:
	rm -f ./sendfile ./recvfile ./send/sendfile ./recv/recvfile