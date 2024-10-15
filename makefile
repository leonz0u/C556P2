CC = g++
#for debug
CFLAGS = -g -Wall
#for release
#CFLAGS = -O2 -Wall

all: recvfile sendfile

sendfile: sendfile.cpp
	$(CC) $(CFLAGS) -o sendfile sendfile.cpp

recvfile: recvfile.cpp
	$(CC) $(CFLAGS) -o recvfile recvfile.cpp

clean:
	rm -f recvfile sendfile
