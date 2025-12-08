#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../../include/clientCommands.h"
#include "../../include/protocol.h"
#include "../../include/network.h"
#include "../../include/utils.h"

// ================================================================
// DECLARATIONS (networkClient functions)
// ================================================================
int uploadFile(int sock, const char *localPath, const char *remotePath);
int downloadFile(int sock, const char *remotePath, const char *localPath);

// ================================================================
// WRAPPERS — required by clientMain.c
// ================================================================
int clientUpload(int sock, const char *localPath, const char *remotePath)
{
    return uploadFile(sock, localPath, remotePath);
}

int clientDownload(int sock, const char *remotePath, const char *localPath)
{
    return downloadFile(sock, remotePath, localPath);
}

// ================================================================
// TOKENIZER — single source of truth
// ================================================================
int tokenize(char *input, char *tokens[], int maxTokens)
{
    int count = 0;
    char *tok = strtok(input, " ");
    while (tok && count < maxTokens) {
        tokens[count++] = tok;
        tok = strtok(NULL, " ");
    }
    return count;
}

// ================================================================
// SEND COMMAND HELPER (no extra payload)
// ================================================================
static int sendSimpleCommand(int sock, int cmd, const char *arg1, const char *arg2, const char *arg3)
{
    ProtocolMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.command = cmd;

    if (arg1) strncpy(msg.arg1, arg1, ARG_SIZE);
    if (arg2) strncpy(msg.arg2, arg2, ARG_SIZE);
    if (arg3) strncpy(msg.arg3, arg3, ARG_SIZE);

    sendMessage(sock, &msg);

    ProtocolResponse res;
    if (receiveResponse(sock, &res) < 0) {
        printf("[ERROR] No response from server\n");
        return -1;
    }

    if (res.status == STATUS_OK) return res.dataSize;
    return -1;
}

// ================================================================
// MAIN HANDLER
// ================================================================
int clientHandleInput(int sock, char *input)
{
    char *tokens[10];
    int n = tokenize(input, tokens, 10);
    if (n == 0) return 0;

    char *cmd = tokens[0];

    // -------------------------
    // login
    // -------------------------
    if (strcmp(cmd, "login") == 0) {
        if (n < 2) { printf("Usage: login <username>\n"); return 0; }
        if (sendSimpleCommand(sock, CMD_LOGIN, tokens[1], NULL, NULL) >= 0)
            printf("Logged in as %s\n", tokens[1]);
        else
            printf("[ERROR] login failed\n");
        return 0;
    }

    // -------------------------
    // create_user
    // -------------------------
    if (strcmp(cmd, "create_user") == 0) {
        if (n < 3) { printf("Usage: create_user <username> <permissions>\n"); return 0; }
        if (sendSimpleCommand(sock, CMD_CREATE_USER, tokens[1], tokens[2], NULL) >= 0)
            printf("User created.\n");
        else
            printf("[ERROR] create_user failed\n");
        return 0;
    }

    // -------------------------
    // delete_user
    // -------------------------
    if (strcmp(cmd, "delete_user") == 0) {
        if (n < 2) { printf("Usage: delete_user <username>\n"); return 0; }
        if (sendSimpleCommand(sock, CMD_DELETE_USER, tokens[1], NULL, NULL) >= 0)
            printf("User deleted.\n");
        else
            printf("[ERROR] delete_user failed\n");
        return 0;
    }

    // -------------------------
    // cd
    // -------------------------
    if (strcmp(cmd, "cd") == 0) {
        if (n < 2) { printf("Usage: cd <path>\n"); return 0; }
        if (sendSimpleCommand(sock, CMD_CD, tokens[1], NULL, NULL) < 0)
            printf("[ERROR] cd failed\n");
        return 0;
    }

    // -------------------------
    // list
    // -------------------------
    if (strcmp(cmd, "list") == 0) {
        const char *arg = (n >= 2 ? tokens[1] : "");
        int dataSize = sendSimpleCommand(sock, CMD_LIST, arg, NULL, NULL);
        if (dataSize < 0) { printf("[ERROR] list failed\n"); return 0; }

        char *buffer = malloc(dataSize + 1);
        if (!buffer) {
            printf("[ERROR] Out of memory\n");
            return 0;
        }

        if (recvAll(sock, buffer, dataSize) < 0) {
            printf("[ERROR] list: recv failed\n");
            free(buffer);
            return 0;
        }

        buffer[dataSize] = '\0';
        printf("%s", buffer);
        free(buffer);
        return 0;
    }

    // -------------------------
    // create
    // -------------------------
    if (strcmp(cmd, "create") == 0) {
        if (n < 4) { printf("Usage: create <path> <permissions> <file|dir>\n"); return 0; }
        if (sendSimpleCommand(sock, CMD_CREATE, tokens[1], tokens[2], tokens[3]) < 0)
            printf("[ERROR] create failed\n");
        return 0;
    }

    // -------------------------
    // chmod
    // -------------------------
    if (strcmp(cmd, "chmod") == 0) {
        if (n < 3) { printf("Usage: chmod <path> <permissions>\n"); return 0; }
        if (sendSimpleCommand(sock, CMD_CHMOD, tokens[1], tokens[2], NULL) < 0)
            printf("[ERROR] chmod failed\n");
        return 0;
    }

    // -------------------------
    // move
    // -------------------------
    if (strcmp(cmd, "move") == 0) {
        if (n < 3) { printf("Usage: move <src> <dst>\n"); return 0; }
        if (sendSimpleCommand(sock, CMD_MOVE, tokens[1], tokens[2], NULL) < 0)
            printf("[ERROR] move failed\n");
        return 0;
    }

    // -------------------------
    // delete
    // -------------------------
    if (strcmp(cmd, "delete") == 0) {
        if (n < 2) { printf("Usage: delete <path>\n"); return 0; }
        if (sendSimpleCommand(sock, CMD_DELETE, tokens[1], NULL, NULL) < 0)
            printf("[ERROR] delete failed\n");
        return 0;
    }

    // -------------------------
    // read (PDF format)
    //   read <path>
    //   read -offset=N <path>
    // -------------------------
    if (strcmp(cmd, "read") == 0) {

        ProtocolMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.command = CMD_READ;

        // CASE 1: read <path>
        if (n == 2) {
            strncpy(msg.arg1, tokens[1], ARG_SIZE);        // path
            msg.arg2[0] = '\0';                            // no offset
        }
        // CASE 2: read -offset=N <path>
        else if (n == 3 && strncmp(tokens[1], "-offset=", 8) == 0) {
            const char *offsetStr = tokens[1] + 8;
            strncpy(msg.arg1, tokens[2], ARG_SIZE);        // path
            strncpy(msg.arg2, offsetStr, ARG_SIZE);        // offset
        }
        else {
            printf("Usage:\n");
            printf("  read <path>\n");
            printf("  read -offset=N <path>\n");
            return 0;
        }

        // Pošalji zahtjev
        sendMessage(sock, &msg);

        // Primi header
        ProtocolResponse res;
        if (receiveResponse(sock, &res) < 0 || res.status != STATUS_OK) {
            printf("[ERROR] read failed\n");
            return 0;
        }

        int size = res.dataSize;
        if (size < 0) {
            printf("[ERROR] read: invalid size\n");
            return 0;
        }

        if (size == 0) {
            // Prazan fajl (ili offset iza kraja fajla)
            printf("\n");
            return 0;
        }

        char *buffer = malloc(size + 1);
        if (!buffer) {
            printf("[ERROR] Out of memory\n");
            return 0;
        }

        if (recvAll(sock, buffer, size) < 0) {
            printf("[ERROR] read: recv failed\n");
            free(buffer);
            return 0;
        }

        buffer[size] = '\0';
        printf("%s\n", buffer);
        free(buffer);
        return 0;
    }

    // -------------------------
    // write (VARIJANTA 1: size + payload)
    //   write <path>
    //   write -offset=N <path>
    // -------------------------
    if (strcmp(cmd, "write") == 0) {

        ProtocolMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.command = CMD_WRITE;

        int useOffset = 0;
        int offset = 0;

        // FORMAT 1: write <path>
        if (n == 2) {
            strncpy(msg.arg1, tokens[1], ARG_SIZE);            // path
            msg.arg2[0] = '\0';                                // no offset
        }
        // FORMAT 2: write -offset=N <path>
        else if (n == 3 && strncmp(tokens[1], "-offset=", 8) == 0) {
            useOffset = 1;
            offset = atoi(tokens[1] + 8);
            if (offset < 0) offset = 0;

            strncpy(msg.arg1, tokens[2], ARG_SIZE);            // path
            snprintf(msg.arg2, ARG_SIZE, "%d", offset);        // offset as string
        }
        else {
            printf("Usage:\n");
            printf("  write <path>\n");
            printf("  write -offset=N <path>\n");
            return 0;
        }

        // 1) Pošalji header (komanda + path + offset)
        sendMessage(sock, &msg);

        // 2) Sačekaj ACK da server kaže "spreman sam"
        ProtocolResponse ack;
        if (receiveResponse(sock, &ack) < 0 || ack.status != STATUS_OK) {
            printf("[ERROR] write rejected by server\n");
            return 0;
        }

        // 3) Pročitaj sav input sa STDIN (do EOF / CTRL+D) u dinamički buffer
        char temp[4096];
        char *buffer = NULL;
        int total = 0;
        int capacity = 0;

        int r;
        while ((r = read(STDIN_FILENO, temp, sizeof(temp))) > 0) {
            if (total + r > capacity) {
                int newCap = (capacity == 0) ? 4096 : capacity * 2;
                while (newCap < total + r) newCap *= 2;

                char *newBuf = realloc(buffer, newCap);
                if (!newBuf) {
                    free(buffer);
                    printf("[ERROR] Out of memory while reading stdin\n");
                    return 0;
                }
                buffer = newBuf;
                capacity = newCap;
            }
            memcpy(buffer + total, temp, r);
            total += r;
        }

        // Ako korisnik nije unio ništa (samo ENTER/CTRL+D) — možeš da odlučiš:
        // Ovde šaljemo 0 bajtova, što je ok.
        int size = total;

        // 4) Pošalji veličinu (int)
        if (sendAll(sock, &size, sizeof(int)) < 0) {
            printf("[ERROR] write: failed sending size\n");
            free(buffer);
            return 0;
        }

        // 5) Pošalji sadržaj (ako ga ima)
        if (size > 0) {
            if (sendAll(sock, buffer, size) < 0) {
                printf("[ERROR] write: failed sending data\n");
                free(buffer);
                return 0;
            }
        }

        free(buffer);

        // 6) Sačekaj završni odgovor
        ProtocolResponse finish;
        if (receiveResponse(sock, &finish) >= 0 && finish.status == STATUS_OK) {

            if (useOffset)
                printf("[OK] Wrote %d bytes at offset %d\n", finish.dataSize, offset);
            else
                printf("[OK] Wrote %d bytes\n", finish.dataSize);
        } else {
            printf("[ERROR] write failed\n");
        }

        return 0;
    }

    // -------------------------
    // upload
    // -------------------------
    if (strcmp(cmd, "upload") == 0) {
        if (n < 3) { printf("Usage: upload <local> <remote>\n"); return 0; }
        if (clientUpload(sock, tokens[1], tokens[2]) < 0)
            printf("[ERROR] upload failed\n");
        return 0;
    }

    // -------------------------
    // download
    // -------------------------
    if (strcmp(cmd, "download") == 0) {
        if (n < 3) { printf("Usage: download <remote> <local>\n"); return 0; }
        if (clientDownload(sock, tokens[1], tokens[2]) < 0)
            printf("[ERROR] download failed\n");
        return 0;
    }

    // -------------------------
    // exit
    // -------------------------
    if (strcmp(cmd, "exit") == 0) {
        sendSimpleCommand(sock, CMD_EXIT, NULL, NULL, NULL);
        printf("Client exiting.\n");
        exit(0);
    }

    printf("Unknown command: %s\n", cmd);
    return 0;
}
