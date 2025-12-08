#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/clientCommands.h"
#include "../../include/protocol.h"
#include "../../include/utils.h"
#include "../../include/networkClient.h"

// ------------------------------------------------------------
// Helper: pretty-print server response
// ------------------------------------------------------------
static void printStatus(int status)
{
    if (status == STATUS_OK) {
        printf("[OK]\n");
    } else if (status == STATUS_DENIED) {
        printf("[DENIED]\n");
    } else {
        printf("[ERROR]\n");
    }
}

// ------------------------------------------------------------
// UPLOAD
// ------------------------------------------------------------
int clientUpload(int sock, const char *localPath, const char *remotePath)
{
    FILE *f = fopen(localPath, "rb");
    if (!f) {
        printf("[UPLOAD] Cannot open local file '%s'\n", localPath);
        return -1;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        printf("[UPLOAD] File is empty or unreadable.\n");
        fclose(f);
        return -1;
    }

    // Load file into memory
    char *buffer = malloc(size);
    if (!buffer) {
        printf("[UPLOAD] Out of memory!\n");
        fclose(f);
        return -1;
    }

    fread(buffer, 1, size, f);
    fclose(f);

    // Prepare message
    ProtocolMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.command = CMD_UPLOAD;

    strncpy(msg.arg1, remotePath, ARG_SIZE);
    snprintf(msg.arg2, ARG_SIZE, "%d", size);

    // Send upload request
    sendMessage(sock, &msg);

    // Wait for server ACK
    ProtocolResponse ack;
    if (receiveResponse(sock, &ack) < 0 || ack.status != STATUS_OK) {
        printf("[UPLOAD] Server refused upload.\n");
        free(buffer);
        return -1;
    }

    // Send file data
    if (sendAll(sock, buffer, size) < 0) {
        printf("[UPLOAD] Sending data failed.\n");
        free(buffer);
        return -1;
    }

    free(buffer);

    // Final server response
    ProtocolResponse final;
    if (receiveResponse(sock, &final) < 0 || final.status != STATUS_OK) {
        printf("[UPLOAD] Upload failed.\n");
        return -1;
    }

    printf("[UPLOAD] Complete (%d bytes).\n", final.dataSize);
    return 0;
}

// ------------------------------------------------------------
// DOWNLOAD
// ------------------------------------------------------------
int clientDownload(int sock, const char *remotePath, const char *localPath)
{
    ProtocolMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.command = CMD_DOWNLOAD;
    strncpy(msg.arg1, remotePath, ARG_SIZE);

    sendMessage(sock, &msg);

    ProtocolResponse res;
    if (receiveResponse(sock, &res) < 0 || res.status != STATUS_OK) {
        printf("[DOWNLOAD] Server refused download.\n");
        return -1;
    }

    int size = res.dataSize;
    if (size <= 0) {
        printf("[DOWNLOAD] No data received.\n");
        return -1;
    }

    char *buffer = malloc(size);
    if (!buffer) {
        printf("[DOWNLOAD] Out of memory.\n");
        return -1;
    }

    if (recvAll(sock, buffer, size) < 0) {
        printf("[DOWNLOAD] Data receive failed.\n");
        free(buffer);
        return -1;
    }

    FILE *f = fopen(localPath, "wb");
    if (!f) {
        printf("[DOWNLOAD] Cannot create file '%s'\n", localPath);
        free(buffer);
        return -1;
    }

    fwrite(buffer, 1, size, f);
    fclose(f);
    free(buffer);

    printf("[DOWNLOAD] Complete (%d bytes).\n", size);
    return 0;
}

// ------------------------------------------------------------
// SIMPLE COMMANDS (LOGIN, CD, LIST, CREATE, DELETE, MOVE...)
// ------------------------------------------------------------
int clientSendSimple(int sock, ProtocolMessage *msg)
{
    sendMessage(sock, msg);

    ProtocolResponse res;
    if (receiveResponse(sock, &res) < 0) {
        printf("[SERVER] No response.\n");
        return -1;
    }

    printStatus(res.status);

    // If server sends additional text (LIST, READ, ERROR details)
    if (res.dataSize > 0) {
        char *buffer = malloc(res.dataSize + 1);
        if (!buffer) {
            printf("[CLIENT] Out of memory.\n");
            return -1;
        }

        if (recvAll(sock, buffer, res.dataSize) < 0) {
            printf("[CLIENT] Failed reading extra data.\n");
            free(buffer);
            return -1;
        }

        buffer[res.dataSize] = '\0';
        printf("%s\n", buffer);
        free(buffer);
    }

    return res.status;
}
