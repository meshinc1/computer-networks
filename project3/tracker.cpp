#include <stdio.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <chrono>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h> 
#include <unistd.h>
#include <netdb.h>
#include <thread>
#include <pthread.h>
#include <iostream>
#include <fstream>

#include <vector>
#include <thread>
#include <mutex>
#include <functional>

#include "crc32.h"
#include "PacketHeader.h"

#define PORT 6969
#define PACKETHEADERSIZE 8
#define FILESIZE 30
#define CHUNKSIZE 512000

std::mutex m;

/**
  * Handles requests for torrent files by sending the file and closing the
  * connection.
  */
void handle_request(int connection, char* torrent_buf, unsigned int tor_length, char* client_ip, std::ofstream &log_file)
{
    // Read for possible torrent file request
    PacketHeader header;
    char header_buf[PACKETHEADERSIZE];
    memset(header_buf, '\0', PACKETHEADERSIZE);

    ssize_t recvbyte = recv(connection, header_buf, PACKETHEADERSIZE, MSG_WAITALL);
    if (recvbyte == -1)
    {
        fprintf(stderr, "recv: %s (%d)\n", strerror(errno), errno);
    }
    memcpy(&header, &header_buf, PACKETHEADERSIZE);

    // Log (in)
    m.lock();
    log_file << "in " << client_ip << " " << header.type << " " << header.length << std::endl;
    m.unlock();

    // Send torrent file if so
    if (header.type == 0)
    {
        ssize_t sendbyte = send(connection, torrent_buf, tor_length + PACKETHEADERSIZE, MSG_NOSIGNAL);
        if (sendbyte == -1) 
        {
            std::cout << "Error: sending data failed" << std::endl;
            exit(0);
        }
        close(connection);

        // Log (out)
        m.lock();
        log_file << "out " << client_ip << " " << 1 << " " << tor_length << std::endl;
        m.unlock();
    }
}

int main(int argc, char * argv[])
{
    std::string peers_list, input_file, torrent_file, log;

    // Store file paths
    peers_list = argv[1];
    input_file = argv[2];
    torrent_file = argv[3];
    log = argv[4];

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		std::cout << "Error: creating socket failed" << std::endl;
        exit(0);
	}
	// Allow port number to be reused 
	int yes = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
		std::cout << "Error: setting socket option failed" << std::endl;
		exit(0);
	}

	// Bind socket to an address
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY; // INADDR_ANY = 0.0.0.0
	addr.sin_port = htons(PORT);
	if (bind(sock, (sockaddr *) &addr, sizeof(addr)) == -1) {
		std::cout << "Error: binding socket failed" << std::endl;
		exit(0);
	}

    // Read from peerlist
    std::ifstream ifs;

    ifs.open(peers_list, std::ios::binary);
    if (!ifs.is_open())
    {
        printf("Error opening file\n");
        exit(1);
    }

    std::vector<std::string> peers;
    std::string peer_ip;
    
    while (std::getline(ifs, peer_ip))
    {
        peers.push_back(peer_ip);    
    }
    ifs.close();

    // Create torrent file
    // Write peerlist to torrent file
    std::ofstream torrent(torrent_file, std::ios::binary);

    torrent << peers.size() << std::endl;
    for (std::string peer : peers)
    {
        torrent << peer << std::endl;    
    }
    
    // Read chunks from input file
    ifs.open(input_file, std::ios::binary);
    if (!ifs.is_open())
    {
        printf("Error opening file\n");
        exit(1);
    }
    
    char chunk_buf[CHUNKSIZE];
    
    bool file_good = true;
    std::vector<uint32_t> hashes;

    while (file_good)
    {
        if (file_good)
        {
            memset(&chunk_buf, '\0', CHUNKSIZE);
            ifs.read(chunk_buf, CHUNKSIZE);
        } 

        hashes.push_back(crc32(chunk_buf, ifs.gcount()));

        if (ifs.gcount() == 0)
            file_good = false;    
    }
    ifs.close();

    // Write hashes to torrent file
    torrent << hashes.size()-1 << std::endl;
    for (unsigned int i = 0; i < hashes.size()-1; ++i)
    {
        torrent << i << " " << hashes[i] << std::endl;    
    }
    torrent.close();
    // Torrent file done

    // Read and store torrent file into a buffer for sending
    ifs.open(torrent_file, std::ios::binary);
    if (!ifs.is_open())
    {
        printf("Error opening file\n");
        exit(1);
    }
    
    char torrent_buf[CHUNKSIZE];
    memset(&torrent_buf, '\0', CHUNKSIZE);

    // Leave room for packet header
    ifs.read(&torrent_buf[PACKETHEADERSIZE], CHUNKSIZE - PACKETHEADERSIZE);

    PacketHeader tor_header;
    tor_header.type = 1;
    tor_header.length = ifs.gcount();
    
    ifs.close();

    memcpy(&torrent_buf, &tor_header, PACKETHEADERSIZE);


	if (listen(sock, 10) == -1) 
    { 
        std::cout << "Error: listening socket failed" << std::endl; 
		exit(0);
    } 
    socklen_t addr_len = sizeof(addr);

    // Create log file
    std::ofstream log_file(log, std::ios::binary);
     
    while (true)
    {
        // Accept any incoming connection requests
        int connection = accept(sock, (sockaddr *) &addr, &addr_len);
        if (connection == -1) {
            std::cout << "Error: accepting connection failed" << std::endl;
            exit(0);
        }

        // Resolve address of client for logging
        char client_ip[16];
        inet_ntop(AF_INET, &addr.sin_addr, client_ip, addr_len);

        // Spawn and detach thread for handling any connection requests
        std::thread (handle_request, connection, torrent_buf, tor_header.length, client_ip, std::ref(log_file)).detach();
    }

    /*
	char data[FILESIZE];
	memset(data, 0, sizeof(data));
	ssize_t recvbyte = recv(connection, data, FILESIZE, MSG_WAITALL);
	if (recvbyte == -1)
	{
		fprintf(stderr, "recv: %s (%d)\n", strerror(errno), errno);
	}
	std::ofstream out_file;
	out_file.open("new-file.txt", std::ios::binary);
	out_file.write(data, recvbyte);
	out_file.close();

	std::cout << "Successfully Received File" << std::endl;

	std::thread t(start_thread);
	t.join();
    */

    log_file.close();
	return 0;
}
