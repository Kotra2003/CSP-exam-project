#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../../include/networkClient.h"
#include "../../include/network.h"

// ------------------------------------------------------------
// Connect to server
// ------------------------------------------------------------
int connectToServer(const char *ip, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    return sock;
}

// ------------------------------------------------------------
// Reliable send
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
            fprintf(stderr, "sendAll: connection closed\n");
            return -1;
        }

        total += sent;
    }

    return 0;
}

// ------------------------------------------------------------
// Reliable recv
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
            fprintf(stderr, "recvAll: connection closed\n");
            return -1;
        }

        total += r;
    }

    return 0;
}
