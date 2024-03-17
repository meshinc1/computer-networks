#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <chrono>

const char ARGS_ERROR[] = "Error: invalid arguments\n"; 
const char PORT_ERROR[] = "Error: port number not in range\n";
const char TIME_ERROR[] = "Error: time argument must be greater than 0\n";
const char SOCK_OPEN_ERROR[] = "Error opening socket\n";
const char SOCK_BIND_ERROR[] = "Error binding socket\n";
const char SOCK_ACPT_ERROR[] = "Error on accept\n";
const char SOCK_READ_ERROR[] = "Error reading from socket\n";
const char SOCK_WRIT_ERROR[] = "Error writing to socket\n";

const char SERV_CONC_ERROR[] = "Error connecting to server\n";
const char ADDRESS_ERROR[] = "Invalid address\n";

void error(const char *msg)
{
    printf("%s", msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        error(ARGS_ERROR);

    // Server Mode
    if (strcmp(argv[1], "-s") == 0)
    {
        int sockfd, newsockfd, portno, n;
        unsigned int clilen;
        char buffer[1000] = {0};
        struct sockaddr_in serv_addr, cli_addr;
       
        // Validate server arguments 
        if (argc != 4)
        {
            error(ARGS_ERROR);
        }
        portno = atoi(argv[3]);
        if (portno < 1024 || portno > 65535)
        {
            error(PORT_ERROR);
        } 

        // Create server socket
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
            error(SOCK_OPEN_ERROR);
        
        // Bind address to socket
        serv_addr = {0};
        
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(portno);
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
        {
            error(SOCK_BIND_ERROR);
        }

        // Listen for connection requests
        listen(sockfd, 5);
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr*) &cli_addr, &clilen);
        if (newsockfd < 0)
            error(SOCK_ACPT_ERROR);

        // Timer for measuring throughput
        auto start = std::chrono::steady_clock::now();

        bool fin_recieved = false;  
  
        char last = '1';
        unsigned int data_recieved = 0;
        while(true)
        {
            bzero(buffer, 999);
            n = read(newsockfd, buffer, 999);    
            if (n < 0) error(SOCK_READ_ERROR);

            // Check for 'FIN' msg in buffer
            for (unsigned int i = 0; i < strlen(buffer)-2; ++i)
            {
                if (buffer[i] == 'F' && buffer[i+1] == 'I' && buffer[i+2] == 'N')
                {
                    printf("broke\n");
                    fin_recieved = true;
                }    
            }

            if (fin_recieved)
                break;

            // Ensure same packet is not read twice by checking for '1'
            // character
            unsigned int j = 0;
            for (; j < strlen(buffer); ++j)
            {
                if (buffer[j] == '1')
                    break;
            }
            if (last != buffer[j])
                data_recieved++;

            last = buffer[j];
        }

        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        bzero(buffer, 999);
        n = write(newsockfd, "ACK", 3);
        if (n < 0) error(SOCK_WRIT_ERROR);

        printf("Recieved=%d KB, Rate=%f Mbps\n", data_recieved, (double)data_recieved / elapsed * 8.0/1000);
    }

    // Client Mode
    if (strcmp(argv[1], "-c") == 0)
    {
        int sockfd, portno, n;
        double time;
        struct sockaddr_in serv_addr;
 
        char buffer[1000] = {0};

        // Validate client arguments
        if (argc != 8)
            error(ARGS_ERROR);

        portno = atoi(argv[5]);
        if (portno < 1024 || portno > 65535)
            error(PORT_ERROR);

        time = atof(argv[7]);
        if (time <= 0)
            error(TIME_ERROR);

        // Create client socket
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
            error(SOCK_OPEN_ERROR);

        serv_addr = {0};
        
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(portno);

        // Convert IP address from text to binary
        // Referenced: 'https://www.geeksforgeeks.org/socket-programming-cc/'
        if (inet_pton(AF_INET, argv[3], &serv_addr.sin_addr) <= 0)
            error(ADDRESS_ERROR);

        if (connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
            error(SERV_CONC_ERROR);
          
        // Timer for measuring throughput 
        auto start = std::chrono::steady_clock::now();
  
        unsigned int data_sent = 0;
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <= time)
        {
            // Sets the character in the buffer to '1' for every other packet,
            // to prevent redundant reads
            if (data_sent % 2 == 0)
                buffer[0] = '0';
            else
                buffer[0] = '1';

            for (int i = 1; i < 1000; ++i)
            {
                buffer[i] = '0';    
            }
            n = write(sockfd, buffer, strlen(buffer));
            if (n < 0)
                error(SOCK_WRIT_ERROR);
            
            bzero(buffer, 999);    
            data_sent++;
        }
        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        bzero(buffer, 999);    
        n = write(sockfd, "FIN", 3);
        if (n < 0) error(SOCK_WRIT_ERROR);

        printf("Sent=%d KB, Rate=%f Mbps\n", data_sent, (double)data_sent / elapsed * 8.0/1000);
    }

    return 0;
}
