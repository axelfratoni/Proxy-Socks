#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#define MYPORT 1080
#define MAXPENDING 10
#define BUFFER_SIZE 10000

static pthread_t cThread;

int hostname_to_ip(char * hostname , char* ip)
{
    struct hostent *he;
    struct in_addr **addr_list;
    int i;

    if ( (he = gethostbyname( hostname ) ) == NULL)
    {
        // get the host info
        herror("gethostbyname");
        return 1;
    }

    addr_list = (struct in_addr **) he->h_addr_list;

    for(i = 0; addr_list[i] != NULL; i++)
    {
        //Return the first one;
        strcpy(ip , inet_ntoa(*addr_list[i]) );
        return 0;
    }

    return 1;
}

static void* handle_read (void* p_socket) {
    int* socks = (int *) p_socket; // 0 -> sock viejo // 1 -> sock nuevo
    ssize_t recvSize;
    uint8_t * buffer = malloc(BUFFER_SIZE*sizeof(buffer[0]));
    while(recvSize = recv(socks[1], buffer, BUFFER_SIZE-1,0)){
        send(socks[0], buffer, recvSize, 0);
    }
}

static void* handle_connection (void* p_socket) {
    printf("Handling connection\n");
    uint8_t * buffer = malloc(BUFFER_SIZE*sizeof(buffer[0]));
    uint8_t * sendBuffer = malloc(BUFFER_SIZE*sizeof(buffer[0]));
    char ip[100];
    char* host = malloc(200*sizeof host[0]);
	int sockfd = *((int *) p_socket);
	int newSock;
	free((int *) p_socket);
	int step = 0;
	ssize_t recvSize;
	while(recvSize = recv(sockfd, buffer, BUFFER_SIZE-1, 0)){
		if (step == 0) {
            printf("Ver: %d | NMethods: %d | Methods: ", buffer[0], buffer[1]);
            for (int i = 0; i < buffer[1]; i++) {
                printf(" %d", buffer[2 + i]);
            }
            printf("\n");
            sendBuffer[0] = buffer[0];
            sendBuffer[1] = 0;
            send(sockfd, sendBuffer, 2, 0);
            step++;
		} else if (step == 1) {
            printf("Ver: %d | CMD: %d", buffer[0], buffer[1]);
            switch(buffer[1]) {
                case 2:case 3:
                printf("\nCannot resolve this CMD\n");
                buffer[3] = 0; // Puenteo el siguiente switch
                buffer[1] = 5; // Connection refused
            }
            printf(" | Addr Type: ");
            switch(buffer[3]) {
                case 1:
                printf("IPv4");
                buffer[1] = 0;

                break;

                case 3:
                printf("Domain Name | Dest Addr: ");
                for (int i = 0; i < buffer[4]; i++) {
                    host[i] = buffer[5 + i];
                    printf("%c", buffer[5 + i]);
                }
                host[buffer[4]] = 0;
                if (hostname_to_ip(host,ip) == 0) {
                    printf("\nIP: %s",ip);
                } else {
                    ip[0] = buffer[4];
                    ip[1] = ".";
                    ip[2] = buffer[5];
                    ip[3] = ".";
                    ip[4] = buffer[6];
                    ip[5] = ".";
                    ip[6] = buffer[7];
                    ip[7] = 0;
                    printf("%s",ip);
                }
                buffer[1] = 0;
                break;

                case 4:
                printf("IPv6");
                break;
            }
            uint16_t port = (((uint16_t)buffer[recvSize - 2]) << 8) | ((uint16_t)buffer[recvSize - 1]);
            char* portString = malloc(10 * sizeof portString[0]);
            sprintf(portString, "%d", port);
            printf(" | Port: %s (%d %d)\n", portString,buffer[recvSize - 2],buffer[recvSize - 1]);

            struct addrinfo hints, *res;
            memset(&hints, 0, sizeof hints);
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            getaddrinfo(ip, portString, &hints, &res);
            newSock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            if (newSock < 0) {
                buffer[1] = 5;
                printf("failed new socket()\n");
            } else {
                if (connect(newSock, res->ai_addr, res->ai_addrlen) < 0) {
                    buffer[1] = 5;
                    printf("failed connect()\n");
                } else {
                    printf("New sock connected\n");
                    buffer[1] = 0;
                }
            }
            send(sockfd, buffer, recvSize, 0);
            int * p_socket = malloc(2*sizeof(int));
            p_socket[0] = sockfd;
            p_socket[1] = newSock;
            pthread_create(&cThread, NULL, handle_read, (void *) p_socket);
            step++;
		} else if (step == 2) {
            for (int i =0; i < 100; i++) {
                printf("%c", buffer[i]);
            }
            send(newSock, buffer, recvSize, 0);
            //send(newSock, buffer, recvSize, 0);
            recvSize = recv(newSock, buffer, BUFFER_SIZE-1,0);
            send(sockfd, buffer, recvSize, 0);
		}
	}
	printf("Connection ended\n");
}

int main()
{
    printf("Connecting... ");

    int servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (servSock < 0){
        printf("socket() failed\n");
        return -1;
    }
    struct sockaddr_in servAddr;
    memset(&servAddr, 0, sizeof servAddr);
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(MYPORT);

    if (bind(servSock,(struct sockaddr*) &servAddr, sizeof servAddr) < 0) {
        printf("bind() failed\n");
        return -1;
    }

    printf("Connection ready\n");

    if (listen(servSock, MAXPENDING) < 0) {
        printf("listen() failed\n");
        return 1;
    }

    while (1) {
        struct sockaddr_in clntAddr;
        socklen_t clntAddrLen = sizeof clntAddr;
        int clntSock = accept(servSock, (struct sockaddr*) &clntAddr, &clntAddrLen);
        if (clntSock < 0) {
            printf("accept() failed\n");
            return 1;
        }
        char clnName[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &clntAddr.sin_addr.s_addr, clnName, sizeof clnName) != NULL){
            printf("Handling client %s/%d\n", clnName, ntohs(clntAddr.sin_port));
        } else {
            printf("Unable to get client addres\n");
        }

        int * p_socket = malloc(sizeof(int));
    	*p_socket = clntSock;

        pthread_create(&cThread, NULL, handle_connection, (void *) p_socket);
    }
    close(servSock);
    pthread_exit(NULL);
    return 0;
}
