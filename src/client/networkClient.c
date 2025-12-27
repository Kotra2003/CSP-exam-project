// src/client/networkClient.c

#include <stdio.h>      // Standard I/O
#include <stdlib.h>     // exit()
#include <string.h>     // memset()
#include <unistd.h>     // close()
#include <arpa/inet.h>  // inet_pton()

#include "../../include/networkClient.h" // Client-side networking API
#include "../../include/network.h"       // Shared networking helpers
#include "../../include/protocol.h"      // Protocol definitions

// ------------------------------------------------------------
// Connect to server (client side)
// Creates a TCP socket and establishes a connection
// ------------------------------------------------------------
int connectToServer(const char *ip, int port)
{
    // Create TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    // Prepare server address structure
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    // Convert IP address from string to binary form
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return -1;
    }

    // Establish connection to server
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    return sock;
}

// ------------------------------------------------------------
// Reliable send (CLIENT SIDE)
// Sends exactly 'size' bytes over TCP.
// If the connection is lost, the client terminates.
// ------------------------------------------------------------
int sendAll(int sock, const void *buffer, int size)
{
    int total = 0;

    // Loop until all bytes are sent
    while (total < size) {
        int sent = send(sock, (const char *)buffer + total, size - total, 0);

        if (sent < 0) {
            perror("sendAll");
            fprintf(stderr, "[FATAL] Connection to server lost. Exiting client.\n");
            exit(1);
        }
        if (sent == 0) {
            fprintf(stderr, "[FATAL] Connection closed by server (send).\n");
            exit(1);
        }

        total += sent;
    }

    return 0;
}

// ------------------------------------------------------------
// Reliable receive (CLIENT SIDE)
// Receives exactly 'size' bytes over TCP.
// If the connection is lost, the client terminates.
// ------------------------------------------------------------
int recvAll(int sock, void *buffer, int size)
{
    int total = 0;

    // Loop until all requested bytes are received
    while (total < size) {
        int r = recv(sock, (char *)buffer + total, size - total, 0);

        if (r < 0) {
            perror("recvAll");
            fprintf(stderr, "[FATAL] Connection to server lost. Exiting client.\n");
            exit(1);
        }
        if (r == 0) {
            fprintf(stderr, "[FATAL] Connection closed by server (recv).\n");
            exit(1);
        }

        total += r;
    }

    return 0;
}

// ------------------------------------------------------------
// UPLOAD FILE (client - server)
// ------------------------------------------------------------
int uploadFile(int sock, const char *localPath, const char *remotePath)
{
    // Open local file in binary read mode
    FILE *f = fopen(localPath, "rb");
    if (!f) {
        perror("fopen");
        return -1;
    }

    // Determine file size
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 0) {
        perror("ftell");
        fclose(f);
        return -1;
    }

    // Safety check: prevent huge file uploads
    if (fsize > MAX_FILE_SIZE) {
        printf("[UPLOAD] File too large (%ld bytes, max %d)\n",
               fsize, MAX_FILE_SIZE);
        fclose(f);
        return -1;
    }

    int size = (int)fsize;

    // Prepare upload request header
    ProtocolMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.command = CMD_UPLOAD;
    strncpy(msg.arg1, remotePath, sizeof(msg.arg1));
    snprintf(msg.arg2, sizeof(msg.arg2), "%d", size);

    // Send upload header to server
    sendAll(sock, &msg, sizeof(msg));

    // Receive server response (accept / deny upload)
    ProtocolResponse res;
    recvAll(sock, &res, sizeof(res));

    if (res.status != STATUS_OK) {
        printf("[UPLOAD] Server refused upload\n");
        fclose(f);
        return -1;
    }

    // Load file content into memory
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
            printf("[UPLOAD] fread mismatch (%d/%d)\n", readBytes, size);
            free(buffer);
            fclose(f);
            return -1;
        }
    }
    fclose(f);

    // Send file content to server
    if (size > 0) {
        sendAll(sock, buffer, size);
        free(buffer);
    }

    // Receive final server confirmation
    recvAll(sock, &res, sizeof(res));
    if (res.status != STATUS_OK) {
        printf("[UPLOAD] Final server response not OK\n");
        return -1;
    }

    return 0;
}

// ------------------------------------------------------------
// DOWNLOAD FILE (server - client)
// ------------------------------------------------------------
int downloadFile(int sock, const char *remotePath, const char *localPath)
{
    // Prepare download request
    ProtocolMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.command = CMD_DOWNLOAD;
    strncpy(msg.arg1, remotePath, sizeof(msg.arg1));

    // Send download request to server
    sendAll(sock, &msg, sizeof(msg));

    // Receive server response
    ProtocolResponse res;
    recvAll(sock, &res, sizeof(res));

    if (res.status != STATUS_OK) {
        printf("[DOWNLOAD] Server refused download\n");
        return -1;
    }

    int size = res.dataSize;

    // Safety check: validate received file size
    if (size < 0 || size > MAX_FILE_SIZE) {
        printf("[DOWNLOAD] Invalid or too large file (%d bytes)\n", size);
        return -1;
    }

    // Handle empty file case
    if (size == 0) {
        FILE *fempty = fopen(localPath, "wb");
        if (!fempty) {
            perror("fopen");
            return -1;
        }
        fclose(fempty);
        return 0;
    }

    // Allocate buffer for incoming file data
    char *buffer = malloc(size);
    if (!buffer) {
        printf("[DOWNLOAD] Out of memory\n");
        return -1;
    }

    // Receive file content
    recvAll(sock, buffer, size);

    // Write file to disk
    FILE *f = fopen(localPath, "wb");
    if (!f) {
        perror("fopen");
        free(buffer);
        return -1;
    }

    int written = (int)fwrite(buffer, 1, size, f);
    if (written != size) {
        printf("[DOWNLOAD] fwrite mismatch (%d/%d)\n", written, size);
    }

    fclose(f);
    free(buffer);

    return 0;
}

