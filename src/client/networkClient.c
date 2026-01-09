// src/client/networkClient.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../../include/network.h"
#include "../../include/protocol.h"

// ------------------------------------------------------------
// Connect to server (client side)
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
// Send exactly size bytes over TCP
// ------------------------------------------------------------
int sendAll(int sock, const void *buffer, int size)
{
    int total = 0;

    while (total < size) {
        int sent = send(sock, (const char *)buffer + total, size - total, 0);

        if (sent < 0) {
            perror("sendAll");
            fprintf(stderr, "[FATAL] Connection to server lost.\n");
            exit(1);
        }

        if (sent == 0) {
            fprintf(stderr, "[FATAL] Connection closed by server.\n");
            exit(1);
        }

        total += sent;
    }

    return 0;
}


// ------------------------------------------------------------
// Receive exactly size bytes from server
// ------------------------------------------------------------
int recvAll(int sock, void *buffer, int size)
{
    int total = 0;

    while (total < size) {
        int r = recv(sock, (char *)buffer + total, size - total, 0);

        if (r < 0) {
            perror("recvAll");
            fprintf(stderr, "[FATAL] Connection lost.\n");
            exit(1);
        }
        if (r == 0) {
            fprintf(stderr, "[FATAL] Server closed connection.\n");
            exit(1);
        }

        total += r;
    }

    return 0;
}

// ------------------------------------------------------------
// Upload file to server
// ------------------------------------------------------------
int uploadFile(int sock, const char *localPath, const char *remotePath)
{
    FILE *f = fopen(localPath, "rb");
    if (!f) {
        perror("fopen");
        return -1;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 0) {
        perror("ftell");
        fclose(f);
        return -1;
    }

    // Limit upload size
    if (fsize > MAX_FILE_SIZE) {
        printf("[UPLOAD] File too large (%ld bytes)\n", fsize);
        fclose(f);
        return -1;
    }

    int size = (int)fsize;

    // Send upload request
    ProtocolMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.command = CMD_UPLOAD;
    strncpy(msg.arg1, remotePath, sizeof(msg.arg1));
    snprintf(msg.arg2, sizeof(msg.arg2), "%d", size);

    sendAll(sock, &msg, sizeof(msg));

    // Server response
    ProtocolResponse res;
    recvAll(sock, &res, sizeof(res));
    if (res.status != STATUS_OK) {
        printf("[UPLOAD] Server refused upload\n");
        fclose(f);
        return -1;
    }

    // Read file into buffer
    char *buffer = NULL;
    if (size > 0) {
        buffer = malloc(size);
        if (!buffer) {
            printf("[UPLOAD] Out of memory\n");
            fclose(f);
            return -1;
        }

        int readBytes = (int)fread(buffer, 1, size, f);
        if (readBytes != size) {
            printf("[UPLOAD] Read error\n");
            free(buffer);
            fclose(f);
            return -1;
        }
    }
    fclose(f);

    // Send file data
    if (size > 0) {
        sendAll(sock, buffer, size);
        free(buffer);
    }

    // Final confirmation
    recvAll(sock, &res, sizeof(res));
    if (res.status != STATUS_OK) {
        printf("[UPLOAD] Upload failed\n");
        return -1;
    }

    return 0;
}

// ------------------------------------------------------------
// Download file from server
// ------------------------------------------------------------
int downloadFile(int sock, const char *remotePath, const char *localPath)
{
    // Send download request
    ProtocolMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.command = CMD_DOWNLOAD;
    strncpy(msg.arg1, remotePath, sizeof(msg.arg1));

    sendAll(sock, &msg, sizeof(msg));

    // Server response
    ProtocolResponse res;
    recvAll(sock, &res, sizeof(res));

    if (res.status != STATUS_OK) {
        printf("[DOWNLOAD] Server refused download\n");
        return -1;
    }

    int size = res.dataSize;

    // Check received size
    if (size < 0 || size > MAX_FILE_SIZE) {
        printf("[DOWNLOAD] Invalid file size (%d bytes)\n", size);
        return -1;
    }

    // Empty file
    if (size == 0) {
        FILE *fempty = fopen(localPath, "wb");
        if (!fempty) {
            perror("fopen");
            return -1;
        }
        fclose(fempty);
        return 0;
    }

    // Receive file data
    char *buffer = malloc(size);
    if (!buffer) {
        printf("[DOWNLOAD] Out of memory\n");
        return -1;
    }

    recvAll(sock, buffer, size);

    // Write to disk
    FILE *f = fopen(localPath, "wb");
    if (!f) {
        perror("fopen");
        free(buffer);
        return -1;
    }

    int written = (int)fwrite(buffer, 1, size, f);
    if (written != size) {
        printf("[DOWNLOAD] Write error (%d/%d)\n", written, size);
    }

    fclose(f);
    free(buffer);

    return 0;
}