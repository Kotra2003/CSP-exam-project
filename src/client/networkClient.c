#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../../include/network.h"

// Connect to the server using IP and port
int connectToServer(const char *ip, int port)
{
    int sock;
    struct sockaddr_in serverAddr;

    // Create socket (socket for sending and listening)
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        // print simple error
        perror("socket");
        return -1;
    }

    // Prepare server address (again we need to clean everything before use)
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;       // IPv4
    serverAddr.sin_port = htons(port);     // Convert port to network byte order

    // Convert IP string to binary format
    if (inet_pton(AF_INET, ip, &serverAddr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return -1;
    }

    // Try to connect to server (line bind on server side connect needs a address in binary to be stored)
    if (connect(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    // Successful connection
    return sock;
}

// Send all bytes to the socket (without this it is possible for send to send only a portion of data)
int sendAll(int sock, const void *buffer, int size)
{
    int totalSent = 0;

    // Keep sending until everything is sent
    while (totalSent < size) {
        int sent = send(sock, (char *)buffer + totalSent, size - totalSent, 0);     // We want to be sure everything is sent and also sent in right order (not overwritten)
        if (sent <= 0) {
            perror("send");
            return -1;
        }
        totalSent += sent;
    }

    return 0;
}

// Receive exactly size bytes from the socket
int recvAll(int sock, void *buffer, int size)
{
    int totalRecv = 0;

    // Keep receiving until all expected bytes arrive
    while (totalRecv < size) {
        int r = recv(sock, (char *)buffer + totalRecv, size - totalRecv, 0); // Like in sendAll we want to be sure noting is overwritten
        if (r <= 0) {
            perror("recv");
            return -1;
        }
        totalRecv += r;
    }

    return 0;
}
