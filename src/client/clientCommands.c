#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/clientCommands.h"
#include "../../include/protocol.h"
#include "../../include/utils.h"
#include "../../include/networkClient.h"

// =====================================================================
// Upload
// =====================================================================
int clientUpload(int sock, const char *localPath, const char *remotePath)
{
    FILE *f = fopen(localPath, "rb");
    if (!f) {
        printf("Could not open local file.\n");
        return -1;
    }

    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(size);
    if (!buffer) {
        fclose(f);
        return -1;
    }

    fread(buffer, 1, size, f);
    fclose(f);

    ProtocolMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.command = CMD_UPLOAD;
    strncpy(msg.arg1, remotePath, sizeof(msg.arg1));
    snprintf(msg.arg2, sizeof(msg.arg2), "%d", size);

    sendMessage(sock, &msg);

    ProtocolResponse res;
    if (receiveResponse(sock, &res) < 0 || res.status != STATUS_OK) {
        printf("Server refused upload.\n");
        free(buffer);
        return -1;
    }

    sendAll(sock, buffer, size);
    free(buffer);

    if (receiveResponse(sock, &res) < 0 || res.status != STATUS_OK) {
        printf("Upload failed.\n");
        return -1;
    }

    printf("Upload complete.\n");
    return 0;
}

// =====================================================================
// Download
// =====================================================================
int clientDownload(int sock, const char *remotePath, const char *localPath)
{
    ProtocolMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.command = CMD_DOWNLOAD;
    strncpy(msg.arg1, remotePath, sizeof(msg.arg1));

    sendMessage(sock, &msg);

    ProtocolResponse res;
    if (receiveResponse(sock, &res) < 0 || res.status != STATUS_OK) {
        printf("Server refused download.\n");
        return -1;
    }

    int size = res.dataSize;

    char *buffer = malloc(size);
    if (!buffer) return -1;

    if (recvAll(sock, buffer, size) < 0) {
        free(buffer);
        return -1;
    }

    FILE *f = fopen(localPath, "wb");
    if (!f) {
        free(buffer);
        return -1;
    }

    fwrite(buffer, 1, size, f);
    fclose(f);
    free(buffer);

    printf("Download complete.\n");
    return 0;
}

// =====================================================================
// Generic command sender for all simple server commands
// =====================================================================
int clientSendSimple(int sock, ProtocolMessage *msg)
{
    sendMessage(sock, msg);

    ProtocolResponse res;
    if (receiveResponse(sock, &res) < 0) {
        printf("Error receiving response.\n");
        return -1;
    }

    printf("Status: %d\n", res.status);

    if (res.dataSize > 0) {
        char *buffer = malloc(res.dataSize + 1);
        recvAll(sock, buffer, res.dataSize);
        buffer[res.dataSize] = '\0';
        printf("%s\n", buffer);
        free(buffer);
    }

    return res.status;
}
