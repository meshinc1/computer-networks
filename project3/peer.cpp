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

#include <sstream>
#include <vector>
#include <mutex>
#include <algorithm>

#include "crc32.h"
#include "PacketHeader.h"
#include "Chunk.h"

#define SERVER "localhost"
#define FILESIZE 30
#define PACKETHEADERSIZE 8
#define CHUNKSIZE 512000

#define TRACKERPORT 6969
#define PEERPORT 6881

std::mutex m;

/**
  * Adds the chunk ids of the chunks owned by this peer to the chunk directory.
  */
void add_owned_chunks(std::vector<std::vector<std::string>> &chunk_directory, 
    std::string owned_chunks, std::string my_ip)
{
    std::ifstream ifs;
    ifs.open(owned_chunks, std::ios::binary);
    if (!ifs.is_open())
    {
        printf("Error opening file\n");
        exit(1);
    }

    std::string s;
    while (std::getline(ifs, s))
    {
        chunk_directory[std::stoul(s)].push_back(my_ip);
    }
    ifs.close();
}


/**
  * Return the id of the rarest chunk in the chunk_directory that is not already owned by the peer.
  */
int get_rarest_chunk(const std::vector<std::vector<std::string>> &chunk_directory, 
                           std::vector<bool> already_owned)
{
    unsigned int rarest_index = std::distance(already_owned.begin(), std::find(already_owned.begin(), already_owned.end(), false));
    unsigned int rarest_count = chunk_directory[rarest_index].size(); // Num of peers that own the rarest chunk
    
    for (unsigned int i = 0; i < chunk_directory.size(); ++i)
    {
        if (!already_owned[i] && chunk_directory[i].size() < rarest_count)
        {
            rarest_index = i;
            rarest_count = chunk_directory[i].size();    
        }
    }

    return rarest_index;
}


/**
  * Requests all missing chunks from other peers.
  * Returns a vector of Chunk objects.
  */
std::vector<Chunk> get_chunks(std::vector<std::vector<std::string>> &chunk_directory, 
    std::vector<uint32_t> hashes, 
    std::ofstream &log_file, 
    std::string owned_chunks, 
    std::string my_ip)
{
    // Vector indicating all chunks currently owned by this peer (including those received from other peers).
    std::vector<bool> all_owned_chunks(chunk_directory.size(), false);
    for (unsigned int i = 0; i < chunk_directory.size(); ++i)
    {
        if (std::find(chunk_directory[i].begin(), chunk_directory[i].end(), my_ip) != chunk_directory[i].end())
        {
            all_owned_chunks[i] = true;    
        }
    }
    
    std::vector<Chunk> chunks(hashes.size());

    PacketHeader header;
    char header_buf[PACKETHEADERSIZE];
    char request_buf[PACKETHEADERSIZE+4];
    char data_buf[CHUNKSIZE];

    struct sockaddr_in peer_addr;
	peer_addr.sin_family = AF_INET;

    // Request chunks from other peers until all chunks are in possession
    while (std::find(all_owned_chunks.begin(), all_owned_chunks.end(), false) != all_owned_chunks.end())
    {
        
        unsigned int rarest_chunk_id = get_rarest_chunk(chunk_directory, all_owned_chunks);
   
        // Loop until chunk recieved has a valid hash 
        while (true) 
        {    
            // Create socket 
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock == -1) {
                std::cout << "Error: creating socket failed" << std::endl;
                exit(0);
            }

            peer_addr.sin_addr.s_addr = INADDR_ANY; // INADDR_ANY = 0.0.0.0
            peer_addr.sin_port = htons((u_short) PEERPORT);
            
            // Get IP of the peer from which a chunk will be requested using the chunk_directory
            std::string peer = chunk_directory[rarest_chunk_id][0];
            
            if (inet_pton(AF_INET, peer.c_str(), &peer_addr.sin_addr) <= 0)
            {
                printf("Invalid address\n");
                exit(1);
            }
            // Attempt to connect until successful
            while (connect(sock, (sockaddr*) &peer_addr, sizeof(peer_addr)) == -1)
            {
                std::cout << "Error: can't connect to server for chunk request" << std::endl;
            }
           
            header.type = 4;
            header.length = 4;
            memcpy(&request_buf, &header, PACKETHEADERSIZE);

            // Add hash for requested chunk into buffer
            memcpy(&request_buf[PACKETHEADERSIZE], &hashes[rarest_chunk_id], 4);

            ssize_t sendbyte = send(sock, request_buf, PACKETHEADERSIZE+4, MSG_NOSIGNAL);
            if (sendbyte == -1) 
            {
                std::cout << "Error: sending data failed" << std::endl;
                exit(0);
            }
           
            m.lock();
            log_file << "out " << peer << " " << header.type << " " << header.length << std::endl;
            m.unlock();
            
            memset(header_buf, '\0', PACKETHEADERSIZE);
            ssize_t recvbyte = recv(sock, header_buf, PACKETHEADERSIZE, MSG_WAITALL);
            if (recvbyte == -1)
            {
                fprintf(stderr, "recv: %s (%d)\n", strerror(errno), errno);
            }
            memcpy(&header, &header_buf, PACKETHEADERSIZE);

            m.lock();
            log_file << "in " << peer << " " << header.type << " " << header.length << std::endl;
            m.unlock();

            if (header.type == 5)
            {  
                // Read in chunk according to header.length
                memset(data_buf, '\0', CHUNKSIZE);
                ssize_t recvbyte = recv(sock, data_buf, header.length, MSG_WAITALL);
                if (recvbyte == -1)
                {
                    fprintf(stderr, "recv: %s (%d)\n", strerror(errno), errno);
                }

                if (crc32(data_buf, header.length) == hashes[rarest_chunk_id])
                {
                    memcpy(&chunks[rarest_chunk_id].buffer, &data_buf, header.length);
                    chunks[rarest_chunk_id].length = header.length;
                    all_owned_chunks[rarest_chunk_id] = true;
                    break;
                }
            }
        }
    }
    return chunks;
}


/**
  * Write all data to the output file using chunks recieved from other peers
  * and chunks owned by this peer.
  */
void write_to_output(const std::vector<std::vector<std::string>> &chunk_directory,
    const std::vector<Chunk> &chunks, 
    std::string my_ip, 
    std::string input_file, 
    std::string output_file)
{
    // Create output file
    std::ofstream output(output_file, std::ios::binary);

    // Open input file
    std::ifstream ifs;
    ifs.open(input_file, std::ios::binary);
    if (!ifs.is_open())
    {
        printf("Error opening file\n");
        exit(1);
    }
   
    // Loop through all chunk ids and write to the output file sequentially
    for (unsigned int id = 0; id < chunk_directory.size(); ++id)
    {
        // If this chunk (id) is not owned by this peer
        if (std::find(chunk_directory[id].begin(), chunk_directory[id].end(), my_ip) == chunk_directory[id].end())
        {
            output.write(chunks[id].buffer, chunks[id].length);
        }
        else
        {
            char data_buf[CHUNKSIZE];

            ifs.seekg(CHUNKSIZE * id);
            ifs.read(data_buf, CHUNKSIZE);

            output.write(data_buf, ifs.gcount());
        }
    }

    ifs.close();
    output.close();
}


/**
  * Get owned-chunks information from each peer in the network.
  * Populates information into chunk_directory.
  */
void get_available_chunks(std::vector<std::string> peer_ips, 
                          std::vector<std::vector<std::string>> &chunk_directory, 
                          std::string my_ip, 
                          std::ofstream &log_file)
{
    PacketHeader header;
    char header_buf[PACKETHEADERSIZE];
    char data_buf[CHUNKSIZE];

    struct sockaddr_in peer_addr;
	peer_addr.sin_family = AF_INET;

    // Request available-chunks file from every other peer
    for (auto peer : peer_ips)
    {
        if (peer.compare(my_ip) != 0)
        {
            // Create socket 
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock == -1) {
                std::cout << "Error: creating socket failed" << std::endl;
                exit(0);
            }

            peer_addr.sin_addr.s_addr = INADDR_ANY; // INADDR_ANY = 0.0.0.0
            peer_addr.sin_port = htons((u_short) PEERPORT);
           
            if (inet_pton(AF_INET, peer.c_str(), &peer_addr.sin_addr) <= 0)
            {
                printf("Invalid address\n");
                exit(1);
            }
            // Attempt to connect until successful
            while (connect(sock, (sockaddr*) &peer_addr, sizeof(peer_addr)) == -1)
            {
                std::cout << "Error: can't connect to server for request" << std::endl;
            }


            header.type = 2;
            header.length = 0;

            memcpy(&header_buf, &header, PACKETHEADERSIZE);

            ssize_t sendbyte = send(sock, header_buf, PACKETHEADERSIZE, MSG_NOSIGNAL);
            if (sendbyte == -1) 
            {
                std::cout << "Error: sending data failed" << std::endl;
                exit(0);
            }
            
            m.lock();
            log_file << "out " << peer << " " << header.type << " " << header.length << std::endl;
            m.unlock();

            memset(data_buf, '\0', CHUNKSIZE);
            ssize_t recvbyte = recv(sock, data_buf, CHUNKSIZE, MSG_WAITALL);
            if (recvbyte == -1)
            {
                fprintf(stderr, "recv: %s (%d)\n", strerror(errno), errno);
            }
            memcpy(&header, &data_buf, PACKETHEADERSIZE);

            m.lock();
            log_file << "in " << peer << " " << header.type << " " << header.length << std::endl;
            m.unlock();

            if (header.type == 3)
            {
                // Add to chunk directory the chunks that the given peer has
                for (unsigned int i = 0; i < header.length; i += 4)
                {
                    unsigned int chunk= 0;
                    memcpy(&chunk, &data_buf[PACKETHEADERSIZE + i], sizeof(chunk)); 
                    chunk_directory[chunk].push_back(peer);
                } 
            }

            close(sock);
        }
    }
}


/**
  * Handle a request recieved from another peer in the network. This handles both owned-chunk
  * and actual chunk requests.
  */
void handle_request(int connection, 
    std::string owned_chunks, 
    char* client_ip, 
    std::vector<uint32_t> hashes,
    std::string input_file, 
    std::ofstream &log_file)
{
    PacketHeader header;
    char data_buf[CHUNKSIZE];
   
    ssize_t recvbyte = recv(connection, data_buf, PACKETHEADERSIZE, MSG_WAITALL);
    if (recvbyte == -1)
    {
        fprintf(stderr, "recv: %s (%d)\n", strerror(errno), errno);
    }
    memcpy(&header, &data_buf, PACKETHEADERSIZE);
   
    m.lock();
    log_file << "in " << client_ip << " " << header.type << " " << header.length << std::endl;
    m.unlock();
     
    if (header.type == 2) // Owned chunks request
    {
        std::ifstream ifs;
        ifs.open(owned_chunks, std::ios::binary);
        if (!ifs.is_open())
        {
            printf("Error opening file\n");
            exit(1);
        }

        memset(data_buf, '\0', CHUNKSIZE);
       
        unsigned int index = PACKETHEADERSIZE; 
        std::string s;
        while (std::getline(ifs, s))
        {
            unsigned int chunk = std::stoul(s);
            memcpy(&data_buf[index], &chunk, sizeof(chunk));
            index += 4;
        }
        ifs.close();

        header.type = 3;
        header.length = index - PACKETHEADERSIZE;
        memcpy(&data_buf, &header, PACKETHEADERSIZE);

        ssize_t sendbyte = send(connection, data_buf, index, MSG_NOSIGNAL);
        if (sendbyte == -1)
        {   
            std::cout << "Error: sending data failed" << std::endl;
            exit(0);
        }

        m.lock();
        log_file << "out " << client_ip << " " << header.type << " " << header.length << std::endl;
        m.unlock();
    }
    else if (header.type == 4) // Actual chunk request
    {
        uint32_t hash;
        char hash_buf[4];
       
        // Read in hash for requested chunk 
        ssize_t recvbyte = recv(connection, hash_buf, sizeof(hash), MSG_WAITALL);
        if (recvbyte == -1)
        {
            fprintf(stderr, "recv: %s (%d)\n", strerror(errno), errno);
        }
        memcpy(&hash, &hash_buf, sizeof(hash));   
        
        unsigned int requested_id = std::distance(hashes.begin(), std::find(hashes.begin(), hashes.end(), hash));
        char chunk_buf[CHUNKSIZE + PACKETHEADERSIZE];
        
        std::ifstream ifs;
        ifs.open(input_file, std::ios::binary);
        if (!ifs.is_open())
        {
            printf("Error opening file\n");
            exit(1);
        }  

        ifs.seekg(CHUNKSIZE * requested_id, ifs.beg);
        ifs.read(&chunk_buf[PACKETHEADERSIZE], CHUNKSIZE);
       
        header.type = 5;
        header.length = ifs.gcount();
        
        ifs.close();
        memcpy(&chunk_buf, &header, PACKETHEADERSIZE);

        ssize_t sendbyte = send(connection, chunk_buf, header.length + PACKETHEADERSIZE, MSG_NOSIGNAL);
        if (sendbyte == -1)
        {   
            std::cout << "Error: sending data failed" << std::endl;
            exit(0);
        }
        m.lock();
        log_file << "out " << client_ip << " " << header.type << " " << header.length << std::endl;
        m.unlock();
    }

    shutdown(connection, SHUT_WR);
}

/**
  * Runs indefinitely, accepting connections from other peers and spawning 
  * a thread to handle each request.
  */
void connection_handler(std::string owned_chunks, std::vector<uint32_t> hashes, 
    std::string input_file,
    std::ofstream &log_file)
{
    // Create and bind socket for handling requests
    int req_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (req_sock == -1) {
        std::cout << "Error: creating socket failed" << std::endl;
        exit(0);
    }
    // Allow port number to be reused 
    int yes = 1;
    if (setsockopt(req_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        std::cout << "Error: setting socket option failed" << std::endl;
        exit(0);
    }

    // Bind socket to an address
	struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // INADDR_ANY = 0.0.0.0
    addr.sin_port = htons(PEERPORT);
    if (bind(req_sock, (sockaddr *) &addr, sizeof(addr)) == -1) {
        std::cout << "Error: binding socket failed" << std::endl;
        exit(0);
    }

    if (listen(req_sock, 10) == -1)
    {
        std::cout << "Error: listening socket failed" << std::endl;
        exit(0);
    }
    socklen_t addr_len = sizeof(addr);

    while (true)
    {
        // Accept any incoming connection requests
        int connection = accept(req_sock, (sockaddr *) &addr, &addr_len);
        if (connection == -1) {
            std::cout << "Error: accepting connection failed" << std::endl;
            exit(0);
        }
 
        // Resolve address of client for logging
        char client_ip[16];
        inet_ntop(AF_INET, &addr.sin_addr, client_ip, addr_len); 
  
        // Spawn and detach thread for handling requests 
        std::thread(handle_request, connection, owned_chunks, client_ip, hashes, input_file, std::ref(log_file)).detach(); 
    }
}

int main(int argc, char * argv[])
{
    std::string my_ip, tracker_ip, input_file, owned_chunks, output_file, log;

    my_ip = argv[1];
    tracker_ip = argv[2];
    input_file = argv[3];
    owned_chunks = argv[4];
    output_file = argv[5];
    log = argv[6];

	// Create socket 
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		std::cout << "Error: creating socket failed" << std::endl;
        exit(0);
	}

	// connect to the server's binded socket 
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY; // INADDR_ANY = 0.0.0.0
	addr.sin_port = htons((u_short) TRACKERPORT);
	
    if (inet_pton(AF_INET, argv[2], &addr.sin_addr) <= 0)
    {
        printf("Invalid address\n");
        exit(1);
    }
	if(connect(sock, (sockaddr*) &addr, sizeof(addr)) == -1)
	{
		std::cout << "Error: can't connect to server" << std::endl;
        exit(0);
	}

    // Create log file
    std::ofstream log_file(log, std::ios::binary);

    // Request torrent file from tracker
    char header_buf[PACKETHEADERSIZE];
    char torrent_buf[CHUNKSIZE]; 
    PacketHeader header;

    // Loop until received
    while (true)
    {
        header.type = 0;
        header.length = 0;

        memcpy(&header_buf, &header, PACKETHEADERSIZE);

        ssize_t sendbyte = send(sock, header_buf, PACKETHEADERSIZE, MSG_NOSIGNAL);
        if (sendbyte == -1) 
        {
            std::cout << "Error: sending data failed" << std::endl;
            exit(0);
        }

        // Resolve address of tracker for logging
        char tracker_ip[16];
        inet_ntop(AF_INET, &addr.sin_addr, tracker_ip, sizeof(addr)); 

        log_file << "out " << tracker_ip << " " << header.type << " " << header.length << std::endl;

        memset(torrent_buf, '\0', CHUNKSIZE);
        ssize_t recvbyte = recv(sock, torrent_buf, CHUNKSIZE, MSG_WAITALL);
        if (recvbyte == -1)
        {
            fprintf(stderr, "recv: %s (%d)\n", strerror(errno), errno);
        }
        memcpy(&header, &torrent_buf, PACKETHEADERSIZE);

        log_file << "in " << tracker_ip << " " << header.type << " " << header.length << std::endl;

        if (header.type == 1)
        {
            close(sock);
            break;    
        }
    }

    // Parse received torrent file
    std::stringstream tor_stream;
    tor_stream.str(&torrent_buf[PACKETHEADERSIZE]);
    std::string s;
    
    // Create vector for peer ips
    std::getline(tor_stream, s);
    std::vector<std::string> peer_ips(std::stoul(s));

    // Get peers
    for (unsigned int i = 0; i < peer_ips.size(); ++i)
    {
        std::getline(tor_stream, s);
        peer_ips[i] = s;   
    }

    // Create vector for hashes
    std::getline(tor_stream, s);
    std::vector<uint32_t> hashes(std::stoul(s));

    // Store hashes
    for (unsigned int i = 0; i < hashes.size(); ++i)
    {
        std::getline(tor_stream, s);
        hashes[i] = static_cast<uint32_t>(std::stoul(s.substr(s.find(' '))));
    }

    std::vector<std::vector<std::string>> chunk_directory(hashes.size());

    // Spawn thread for requesting owned chunks from other peers
    std::thread t1(get_available_chunks, peer_ips, std::ref(chunk_directory), my_ip, std::ref(log_file));

    // Spawn thread for accepting connections and delegating requests to other threads.
    std::thread t2(connection_handler, owned_chunks, hashes, input_file, std::ref(log_file)); 

    t1.join();

    add_owned_chunks(chunk_directory, owned_chunks, my_ip); 
    std::vector<Chunk> chunks = get_chunks(chunk_directory, hashes, log_file, owned_chunks, my_ip); 

    write_to_output(chunk_directory, chunks, my_ip, input_file, output_file);

    
    t2.join();
	return 0;
}
