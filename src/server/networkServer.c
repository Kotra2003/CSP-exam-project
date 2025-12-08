#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../../include/network.h"

// ------------------------------------------------------------
// Create listening server socket
// ------------------------------------------------------------
int createServerSocket(const char *ip, int port)
{
    int serverFd;
    struct sockaddr_in addr;

    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        perror("socket");
        return -1;
    }

    // Allow quick port reuse (important for dev/testing)
    int opt = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(serverFd);
        return -1;
    }

#ifdef SO_REUSEPORT
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(serverFd);
        return -1;
    }

    if (bind(serverFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(serverFd);
        return -1;
    }

    if (listen(serverFd, 20) < 0) {
        perror("listen");
        close(serverFd);
        return -1;
    }

    return serverFd;
}

// ------------------------------------------------------------
// Accept new client
// ------------------------------------------------------------
int acceptClient(int serverFd)
{
    struct sockaddr_in cliAddr;
    socklen_t len = sizeof(cliAddr);

    int fd = accept(serverFd, (struct sockaddr *)&cliAddr, &len);
    if (fd < 0) {
        perror("accept");
        return -1;
    }

    return fd;
}

// ------------------------------------------------------------
// sendAll - send EXACTLY "size" bytes
// ------------------------------------------------------------
int sendAll(int sock, const void *buffer, int size)
{
    int total = 0;

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
// recvAll - receive EXACTLY "size" bytes
// ------------------------------------------------------------
int recvAll(int sock, void *buffer, int size)
{
    int total = 0;

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
