#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../../include/network.h"

// ------------------------------------------------------------
// Create listening server socket
// Binds to IP:port and starts listening
// ------------------------------------------------------------
int createServerSocket(const char *ip, int port)
{
    int serverFd;
    struct sockaddr_in addr;

    // Create TCP socket
    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        perror("socket");
        return -1;
    }

    // Allow fast port reuse 
    int opt = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(serverFd);
        return -1;
    }

    // Prepare bind address
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    // Convert IP string to binary form
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(serverFd);
        return -1;
    }

    // Bind socket to address
    if (bind(serverFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(serverFd);
        return -1;
    }

    // Start listening for incoming connections
    if (listen(serverFd, 20) < 0) {
        perror("listen");
        close(serverFd);
        return -1;
    }

    return serverFd;
}

// ------------------------------------------------------------
// Accept new client connection
// ------------------------------------------------------------
int acceptClient(int serverFd)
{
    struct sockaddr_in cliAddr;
    socklen_t len = sizeof(cliAddr);

    // Accept incoming connection
    int fd = accept(serverFd, (struct sockaddr *)&cliAddr, &len);
    if (fd < 0) {
        perror("accept");
        return -1;
    }

    return fd;
}

// ------------------------------------------------------------
// sendAll
// Sends EXACTLY "size" bytes over TCP
// ------------------------------------------------------------
int sendAll(int sock, const void *buffer, int size)
{
    int total = 0;

    // Handle partial sends
    while (total < size) {
        int sent = send(sock, (char *)buffer + total, size - total, 0);

        if (sent < 0) {
            perror("sendAll");
            return -1;
        }
        if (sent == 0) {
            fprintf(stderr, "sendAll: connection closed unexpectedly\n");
            return -1;
        }

        total += sent;
    }

    return 0;
}

// ------------------------------------------------------------
// recvAll
// Receives EXACTLY "size" bytes over TCP
// ------------------------------------------------------------
int recvAll(int sock, void *buffer, int size)
{
    int total = 0;

    // Handle partial receives
    while (total < size) {
        int r = recv(sock, (char *)buffer + total, size - total, 0);

        if (r < 0) {
            perror("recvAll");
            return -1;
        }
        if (r == 0) {
            fprintf(stderr, "recvAll: connection closed unexpectedly\n");
            return -1;
        }

        total += r;
    }

    return 0;
}
