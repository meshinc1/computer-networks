#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <chrono>

#include "PacketHeader.h"
#include "Packet.h"
#include "crc32.h"

#define SERVER "localhost"
#define BUFFERSIZE 1472
#define DATASIZE 1456
#define PORT 8001

/**
  * Set values for a packet header.
  */
void set_header(PacketHeader &header, unsigned int type, unsigned int seqNum,
    unsigned int length, unsigned int checksum)
{
    header.type = type;
    header.seqNum = seqNum;
    header.length = length;
    header.checksum = checksum;
}
        
/**
  * Send a packet to a reciever.
  */
void send_packet(PacketHeader &header, char* data_buf, int sockfd, struct sockaddr_in &server_addr, socklen_t &server_len)
{
    char message_buf[BUFFERSIZE];
    memset(&message_buf,'\0', BUFFERSIZE);
    memcpy(&message_buf, &header, sizeof(header));

    if (header.type == 2)
    {
        if (data_buf != nullptr)
        {
            memcpy(&message_buf[sizeof(header)], data_buf, DATASIZE);
        }
        else
        {
            printf("Warning: data packet given null data buffer\n");    
        }
    }

    if (sendto(sockfd, message_buf, sizeof(message_buf), 0, (struct sockaddr *) &server_addr, server_len) == -1)
    {
        perror("Error sending data");
        exit(1);
    }
}

/**
  * Add a packet header to the log.
  */
void add_to_log(PacketHeader &header, std::ofstream &log_file)
{
    log_file << header.type << " " << header.seqNum << " " << header.length 
        << " " << header.checksum << std::endl;    
}


int main(int argc, char *argv[])
{
    struct sockaddr_in server_addr;
    int sockfd, portno, window_size;
    socklen_t server_len = sizeof(server_addr);
    struct timeval timeout;

    char buf[BUFFERSIZE];
    char data_buf[DATASIZE];

    // Validate argument count
    if (argc != 6)
    {
        printf("Error: invalid arguments\n");
        exit(1);    
    }

    // Store function arguments
    portno = atoi(argv[2]);
    window_size = atoi(argv[3]);
    char* input_file(argv[4]);
    std::string log(argv[5]);

    
    // create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        printf("Error creating socket\n");
        exit(1);
    }

    // Set socket timeout time (500ms)
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1)
    {
        perror("Error setting socket options"); 
    }

    memset((char *) &server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portno);    // specify port to connect to 
    
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0)
    {
        printf("Invalid address\n");
        exit(1);    
    }

    // Create log file
    std::ofstream log_file(log + "LogSender.txt");

    // Send start packet
    PacketHeader send_header, rec_packet;
    unsigned int start_seqNum = rand() % RAND_MAX;

    // Loop until ACK received for start packet
    while (true)
    {
        set_header(send_header, 0, start_seqNum, 0, 0);
        send_packet(send_header, nullptr, sockfd, server_addr, server_len);
        add_to_log(send_header, log_file);
         
        memset(buf,'\0', BUFFERSIZE);
        if (recvfrom(sockfd, buf, BUFFERSIZE, 0, (struct sockaddr *) &server_addr, &server_len) > -1)
        {
            printf("Received packet from %s:%d\n", inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));
            memcpy(&rec_packet, buf, sizeof(rec_packet));
            add_to_log(rec_packet, log_file);

            if (rec_packet.type == 3 && rec_packet.seqNum == start_seqNum)
                break;
        }
    }

    // Set port number (again)
    server_addr.sin_port = htons(portno);    // specify port to connect to 
    
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0)
    {
        printf("Invalid address\n");
        exit(1);    
    }


    std::ifstream ifs;
   
    ifs.open(input_file, std::ifstream::binary);
    if (!ifs.is_open())
    {
        printf("Error opening file\n");
        exit(1);
    }
     
    unsigned int data_seqNum = 0; // Seq num of packets sent
    unsigned int acked_seqNum = 0; // seq num of greatest acked packet

    unsigned int buffer_index = 0;
    bool buffer_updated = false;

    bool file_good = true; // State of input file

    // NOTE: acked_seqNum received from rReciever will be the seqNum of the next
    // expected Packet. May have to adjust some conditions below for this (+/- 1).
           
    // Packet send buffer
    std::vector<Packet> packet_buffer(window_size);
  
    // Start reTransmission timer
    auto start = std::chrono::steady_clock::now();
   
    // Data send loop 
    while (file_good || acked_seqNum < data_seqNum)
    {
        // Read data from file and place into buffer
        if (file_good)
        {
            memset(&data_buf,'\0', DATASIZE);
            ifs.read(data_buf, DATASIZE);
        }

        set_header(send_header, 2, data_seqNum, ifs.gcount(), crc32(data_buf, ifs.gcount()));

        // Copy packets into Packet buffer so long as space is available
        // and there is data to be read. (Overwrite ACKed packets)
        if (acked_seqNum + window_size > data_seqNum && file_good)
        {
            packet_buffer[buffer_index].header = send_header;
            memset(&packet_buffer[buffer_index].buffer, '\0', DATASIZE);
            memcpy(&packet_buffer[buffer_index].buffer, &data_buf, sizeof(data_buf));

            buffer_updated = true;
            data_seqNum++;
        }

        if (buffer_updated)
        {
            send_packet(packet_buffer[buffer_index].header, 
                packet_buffer[buffer_index].buffer, sockfd, server_addr, server_len);
            add_to_log(packet_buffer[buffer_index].header, log_file);
                 
            buffer_index = (buffer_index + 1) % window_size;
            buffer_updated = false;
        }

        // If timer expired, send whole packet buffer
        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if ((data_seqNum >= acked_seqNum) && (elapsed > 0.5))
        {
            for (unsigned int i = 0; i < packet_buffer.size(); ++i)
            {
                send_packet(packet_buffer[i].header, 
                    packet_buffer[i].buffer, sockfd, server_addr, server_len);

                add_to_log(packet_buffer[i].header, log_file);
            }

            // Reset timer 
            start = std::chrono::steady_clock::now();
        }

        // Check for ACKs from rReciever
        memset(&buf,'\0', BUFFERSIZE);
        if (recvfrom(sockfd, buf, BUFFERSIZE, 0, (struct sockaddr *) &server_addr, &server_len) > -1)
        {
            memcpy(&rec_packet, buf, sizeof(rec_packet));
            add_to_log(rec_packet, log_file);
             
            if (rec_packet.type == 3)
            {
                acked_seqNum = std::max(rec_packet.seqNum, acked_seqNum);   
                
                start = std::chrono::steady_clock::now();
            }
        }
        
        if (ifs.gcount() == 0)
            file_good = false;
    }
    ifs.close();

    // Loop until ACK received for END packet
    while (true)
    {
        set_header(send_header, 1, start_seqNum, 0, 0);
        send_packet(send_header, nullptr, sockfd, server_addr, server_len);
        add_to_log(send_header, log_file);

        memset(buf,'\0', BUFFERSIZE);
        if (recvfrom(sockfd, buf, BUFFERSIZE, 0, (struct sockaddr *) &server_addr, &server_len) > -1)
        {
            memcpy(&rec_packet, buf, sizeof(rec_packet));
            add_to_log(rec_packet, log_file);

            if (rec_packet.type == 3 && rec_packet.seqNum == start_seqNum)
                break;
        }
    }
    log_file.close();


    // close the socket
    close(sockfd);
    return 0;
}
