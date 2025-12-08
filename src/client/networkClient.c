#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../../include/networkClient.h"
#include "../../include/network.h"
#include "../../include/protocol.h"

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

// ------------------------------------------------------------
// UPLOAD FILE (client → server)
// ------------------------------------------------------------
int uploadFile(int sock, const char *localPath, const char *remotePath)
{
    FILE *f = fopen(localPath, "rb");
    if (!f) {
        perror("fopen");
        return -1;
    }

    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Send upload command
    ProtocolMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.command = CMD_UPLOAD;
    strncpy(msg.arg1, remotePath, sizeof(msg.arg1));
    snprintf(msg.arg2, sizeof(msg.arg2), "%d", size);

    send(sock, &msg, sizeof(msg), 0);

    // Receive OK from server
    ProtocolResponse res;
    if (recv(sock, &res, sizeof(res), 0) <= 0 || res.status != STATUS_OK) {
        printf("[UPLOAD] server refused upload\n");
        fclose(f);
        return -1;
    }

    // Send file content
    char *buffer = malloc(size);
    fread(buffer, 1, size, f);
    fclose(f);

    sendAll(sock, buffer, size);
    free(buffer);

    // Receive final OK
    recv(sock, &res, sizeof(res), 0);
    return (res.status == STATUS_OK) ? 0 : -1;
}

// ------------------------------------------------------------
// DOWNLOAD FILE (server → client)
// ------------------------------------------------------------
int downloadFile(int sock, const char *remotePath, const char *localPath)
{
    // Send download command
    ProtocolMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.command = CMD_DOWNLOAD;
    strncpy(msg.arg1, remotePath, sizeof(msg.arg1));

    send(sock, &msg, sizeof(msg), 0);

    // Receive server response
    ProtocolResponse res;
    if (recv(sock, &res, sizeof(res), 0) <= 0 || res.status != STATUS_OK) {
        printf("[DOWNLOAD] server refused\n");
        return -1;
    }

    int size = res.dataSize;
    if (size <= 0) return -1;

    char *buffer = malloc(size);
    recvAll(sock, buffer, size);

    FILE *f = fopen(localPath, "wb");
    if (!f) {
        perror("fopen");
        free(buffer);
        return -1;
    }

    fwrite(buffer, 1, size, f);
    fclose(f);
    free(buffer);

    return 0;
}
