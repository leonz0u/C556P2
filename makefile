CXX = g++
#for debug
# CXXFLAGS = -g -Wall
# for release
CXXFLAGS = -march=native

all: recvfile sendfile

sendfile: sendfile.cpp
	mkdir -p send
	$(CXX) $(CXXFLAGS) -o ./send/sendfile sendfile.cpp

recvfile: recvfile.cpp
	mkdir -p recv
	$(CXX) $(CXXFLAGS) -o ./recv/recvfile recvfile.cpp

clean:
	rm -f ./sendfile ./recvfile ./send/sendfile ./recv/recvfile