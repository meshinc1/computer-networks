/*
    udp server example
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <iostream>
#include <fstream>
#include <vector>

#include "PacketHeader.h"
#include "Packet.h"
#include "crc32.h"

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
  * Add a packet header to the log.
  */
void add_to_log(PacketHeader &header, std::ofstream &log_file)
{
    log_file << header.type << " " << header.seqNum << " " << header.length
        << " " << header.checksum << std::endl;
}

/**
  * Send an ACK packet to a sender.
  */
void send_ack(unsigned int seqNum, int sockfd, struct sockaddr_in &client_addr, socklen_t &client_len, std::ofstream &log_file)
{
    PacketHeader *header = new PacketHeader();
    set_header(*header, 3, seqNum, 0, 0);

    char message_buf[BUFFERSIZE];
    memset(&message_buf,'\0', BUFFERSIZE);
    memcpy(&message_buf, header, sizeof(*header));

    if (sendto(sockfd, message_buf, sizeof(message_buf), 0, (struct sockaddr *) &client_addr, client_len) == -1)
    {
        perror("Error sending data");
        exit(1);    
    }

    add_to_log(*header, log_file);

    free(header);

}


int main(int argc, char *argv[])
{
    struct sockaddr_in server_addr, client_addr;

    int sockfd, recv_len, portno, window_size;
    socklen_t client_len = sizeof(client_addr);
    char buf[BUFFERSIZE];
    char data_buf[DATASIZE];

    std::string output_dir, log;

    // Validate argument count
    if (argc != 5)
    {
        printf("Error: invalid arguments\n");
        exit(1);    
    }

    // Store function arguments
    portno = atoi(argv[1]);
    window_size = atoi(argv[2]);
    output_dir = argv[3];
    log = argv[4];

    // create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        printf("Error creating socket\n");
        exit(1);
    }

    // zero out the structure
    memset((char *) &server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portno); // bind to port 
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);    // specify port to listen on

    // bind socket to port
    if(bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        printf("Error binding\n");
        exit(1);
    }


    printf("Waiting for data on port %d ...\n", portno);
    fflush(stdout);

    // Create log file
    std::ofstream log_file(log + "LogReceiver.txt");

    // Listen for start request
    memset(buf, '\0', BUFFERSIZE);
    if ((recv_len = recvfrom(sockfd, buf, BUFFERSIZE, 0, (struct sockaddr *) &client_addr, &client_len)) == -1)
    {
        printf("Error receving data\n");
        exit(1);
    }

    printf("Received packet from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    printf("Data: %s\n", buf);


    // Accept connection
    PacketHeader rec_packet;

    memcpy(&rec_packet, &buf, sizeof(rec_packet));
    add_to_log(rec_packet, log_file);

    if (rec_packet.type == 0)
    {
        send_ack(rec_packet.seqNum, sockfd, client_addr, client_len, log_file);
         
        std::cout << "Start received" << std::endl;
        std::cout << "Rec packet: " << rec_packet.type << std::endl;
    }


    unsigned int last_seqNum = 0;
    unsigned int acked_seqNum = 0; // Seq num of greatest acked packet

    unsigned int buffer_index = 0;

    // Packet receive buffer
    std::vector<Packet> packet_buffer(window_size);    
  
    std::ofstream output_file(output_dir + "File-1.out", std::ofstream::binary);
   
    while (true)
    {
        memset(buf, '\0', BUFFERSIZE);
        if ((recv_len = recvfrom(sockfd, buf, BUFFERSIZE, 0, (struct sockaddr *) &client_addr, &client_len)) == -1)
        {   
            printf("Error receving data\n");
            exit(1);
        }
        
        memcpy(&rec_packet, &buf, sizeof(rec_packet));
        add_to_log(rec_packet, log_file);

        if (rec_packet.type == 2)
        {    
            memset(&data_buf, '\0', DATASIZE);
            memcpy(&data_buf, &buf[sizeof(rec_packet)], rec_packet.length);

            if (rec_packet.checksum == crc32(data_buf, rec_packet.length))
            {
                unsigned int index = buffer_index;

                if (rec_packet.seqNum > acked_seqNum &&
                    rec_packet.seqNum <= (acked_seqNum + window_size - 1))
                {
                    index = (index + rec_packet.seqNum - acked_seqNum) % window_size;
                
                
                    packet_buffer[index].header = rec_packet;
                    memset(&packet_buffer[index].buffer, '\0', BUFFERSIZE);
                    memcpy(&packet_buffer[index].buffer, &data_buf, sizeof(data_buf));
                }
                else if (rec_packet.seqNum == acked_seqNum)
                {
                    packet_buffer[index].header = rec_packet;
                    memset(&packet_buffer[index].buffer, '\0', BUFFERSIZE);
                    memcpy(&packet_buffer[index].buffer, &data_buf, sizeof(data_buf));
                }
            }
        }

        // Loop through buffer and write all consecutive packets with
        // expected seq num into the file.
        last_seqNum = acked_seqNum;
        for (unsigned int i = buffer_index; i < buffer_index + window_size; ++i)
        {
            if (packet_buffer[i % window_size].header.seqNum == acked_seqNum)
            {
                output_file << packet_buffer[i % window_size].buffer;
                output_file << std::flush;
                acked_seqNum++; 
            }
            else
            {
                break;
            }
        }
        buffer_index = (buffer_index + acked_seqNum - last_seqNum) % window_size;

        send_ack(acked_seqNum, sockfd, client_addr, client_len, log_file); 

        if (rec_packet.type == 1)
        {

            output_file.close();
            send_ack(rec_packet.seqNum, sockfd, client_addr, client_len, log_file);
            break;    
        }
    }

    log_file.close();

    // close the socket
    close(sockfd);
    return 0;
}
