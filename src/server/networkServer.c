#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../../include/network.h"

// Create a listening socket for the server
int createServerSocket(const char *ip, int port) 
{
    int serverFd;
    struct sockaddr_in address;

    // Create socket
    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        // Simple error print
        perror("socket");
        return -1;
    }

    // Allow quick reuse of the port
    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Set address info
    memset(&address, 0, sizeof(address));   // Deleting all garbage from memeory (fresh start)
    address.sin_family = AF_INET;       // IPv4 (We need to do it beceuse of many structures)
    address.sin_port = htons(port);     // Convert port to network order (to  Big-endian)

    // Convert string IP to binary form
    // ip -> address.sin_addr 
    if (inet_pton(AF_INET, ip, &address.sin_addr) <= 0) {
        perror("inet_pton");
        close(serverFd);
        return -1;
    }

    // Bind our new socket to IP and port
    // To bind it we needed ip to be in binary
    if (bind(serverFd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(serverFd);
        return -1;
    }

    // Put socket in listening mode
    // We are creating 10 slots for listening or queue fo 10 slots (for 10 diff clients)
    if (listen(serverFd, 10) < 0) {
        perror("listen");
        close(serverFd);
        return -1;
    }

    // Return the listening socket
    return serverFd;
}

// Accept a new client connection
int acceptClient(int serverFd) 
{
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);

    // Wait for a connection
    // Creating a socket that is used for sending and receving data form clinet (we can have more of them)
    int clientFd = accept(serverFd, (struct sockaddr *)&clientAddr, &addrLen);
    if (clientFd < 0) {
        perror("accept");
        return -1;
    }

    return clientFd;
}

int sendAll(int sock, const void *buffer, int size)
{
    int total = 0;

    while (total < size) {
        int sent = send(sock, (char*)buffer + total, size - total, 0);
        if (sent <= 0) return -1;
        total += sent;
    }
    return 0;
}

int recvAll(int sock, void *buffer, int size)
{
    int total = 0;

    while (total < size) {
        int received = recv(sock, (char*)buffer + total, size - total, 0);
        if (received <= 0) return -1;
        total += received;
    }
    return 0;
}
