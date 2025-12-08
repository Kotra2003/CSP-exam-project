#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/clientCommands.h"
#include "../../include/protocol.h"
#include "../../include/networkClient.h"
#include "../../include/utils.h"

// Maksimalna dužina ulazne linije
#define INPUT_SIZE 512

// ------------------------------------------------------------
// Help menu
// ------------------------------------------------------------
void printHelp()
{
    printf("\n==== Available Commands ====\n");
    printf("login <username>\n");
    printf("create_user <username>\n");
    printf("cd <dir>\n");
    printf("list [dir]\n");
    printf("create <path> <permissions> <file|dir>\n");
    printf("chmod <path> <permissions>\n");
    printf("move <src> <dst>\n");
    printf("delete <path>\n");
    printf("read <path> <offset> <size>\n");
    printf("write <path> <offset> <size>\n");
    printf("upload <local> <remote>\n");
    printf("download <remote> <local>\n");
    printf("exit\n");
    printf("============================\n\n");
}

// ------------------------------------------------------------
// Split input line into tokens
// ------------------------------------------------------------
int tokenize(char *input, char *tokens[], int maxTokens)
{
    int count = 0;
    char *p = strtok(input, " ");

    while (p && count < maxTokens) {
        tokens[count++] = p;
        p = strtok(NULL, " ");
    }
    return count;
}

// ------------------------------------------------------------
// Build ProtocolMessage from command + args
// ------------------------------------------------------------
void buildSimpleMessage(ProtocolMessage *msg, int command,
                        const char *a1, const char *a2, const char *a3)
{
    memset(msg, 0, sizeof(*msg));
    msg->command = command;

    if (a1) strncpy(msg->arg1, a1, ARG_SIZE);
    if (a2) strncpy(msg->arg2, a2, ARG_SIZE);
    if (a3) strncpy(msg->arg3, a3, ARG_SIZE);
}

// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------
int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Usage: %s <server_ip> <server_port>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int sock = connectToServer(ip, port);
    if (sock < 0) {
        printf("Could not connect.\n");
        return 1;
    }

    printf("Connected to server %s:%d\n", ip, port);
    printf("Type 'help' to see commands.\n");

    char input[INPUT_SIZE];

    // ==========================
    // MAIN COMMAND LOOP
    // ==========================
    while (1)
    {
        printf("> ");
        fflush(stdout);

        if (!fgets(input, INPUT_SIZE, stdin)) {
            printf("Input error.\n");
            break;
        }

        removeNewline(input);
        if (input[0] == '\0')
            continue;

        // Tokenize
        char *t[10];
        int n = tokenize(input, t, 10);
        if (n == 0) continue;

        // Handle HELP
        if (strcmp(t[0], "help") == 0) {
            printHelp();
            continue;
        }

        // Handle EXIT
        if (strcmp(t[0], "exit") == 0) {
            ProtocolMessage msg = {0};
            msg.command = CMD_EXIT;
            sendMessage(sock, &msg);

            ProtocolResponse res;
            receiveResponse(sock, &res);

            printf("Goodbye!\n");
            break;
        }

        ProtocolMessage msg;

        // ======================
        // LOGIN
        // ======================
        if (strcmp(t[0], "login") == 0) {
            if (n < 2) { printf("Usage: login <username>\n"); continue; }
            buildSimpleMessage(&msg, CMD_LOGIN, t[1], NULL, NULL);
            clientSendSimple(sock, &msg);
            continue;
        }

        // ======================
        // CREATE USER
        // ======================
        if (strcmp(t[0], "create_user") == 0) {
            if (n < 2) { printf("Usage: create_user <username>\n"); continue; }
            buildSimpleMessage(&msg, CMD_CREATE_USER, t[1], NULL, NULL);
            clientSendSimple(sock, &msg);
            continue;
        }

        // ======================
        // CD
        // ======================
        if (strcmp(t[0], "cd") == 0) {
            buildSimpleMessage(&msg, CMD_CD, (n >= 2 ? t[1] : ""), NULL, NULL);
            clientSendSimple(sock, &msg);
            continue;
        }

        // ======================
        // LIST
        // ======================
        if (strcmp(t[0], "list") == 0) {
            buildSimpleMessage(&msg, CMD_LIST, (n >= 2 ? t[1] : ""), NULL, NULL);
            clientSendSimple(sock, &msg);
            continue;
        }

        // ======================
        // CREATE
        // ======================
        if (strcmp(t[0], "create") == 0) {
            if (n < 4) {
                printf("Usage: create <path> <permissions> <file|dir>\n");
                continue;
            }
            buildSimpleMessage(&msg, CMD_CREATE, t[1], t[2], t[3]);
            clientSendSimple(sock, &msg);
            continue;
        }

        // ======================
        // CHMOD
        // ======================
        if (strcmp(t[0], "chmod") == 0) {
            if (n < 3) { printf("Usage: chmod <path> <permissions>\n"); continue; }
            buildSimpleMessage(&msg, CMD_CHMOD, t[1], t[2], NULL);
            clientSendSimple(sock, &msg);
            continue;
        }

        // ======================
        // MOVE
        // ======================
        if (strcmp(t[0], "move") == 0) {
            if (n < 3) { printf("Usage: move <src> <dst>\n"); continue; }
            buildSimpleMessage(&msg, CMD_MOVE, t[1], t[2], NULL);
            clientSendSimple(sock, &msg);
            continue;
        }

        // ======================
        // DELETE
        // ======================
        if (strcmp(t[0], "delete") == 0) {
            if (n < 2) { printf("Usage: delete <path>\n"); continue; }
            buildSimpleMessage(&msg, CMD_DELETE, t[1], NULL, NULL);
            clientSendSimple(sock, &msg);
            continue;
        }

        // ======================
        // READ
        // ======================
        if (strcmp(t[0], "read") == 0) {
            if (n < 4) {
                printf("Usage: read <path> <offset> <size>\n");
                continue;
            }
            buildSimpleMessage(&msg, CMD_READ, t[1], t[2], t[3]);
            clientSendSimple(sock, &msg);
            continue;
        }

        // ======================
        // WRITE
        // ======================
        if (strcmp(t[0], "write") == 0) {
            if (n < 4) {
                printf("Usage: write <path> <offset> <size>\n");
                continue;
            }

            int size = atoi(t[3]);
            if (size <= 0 || size > 10000000) {
                printf("Invalid size.\n");
                continue;
            }

            // Notify server of upcoming write
            buildSimpleMessage(&msg, CMD_WRITE, t[1], t[2], t[3]);
            if (clientSendSimple(sock, &msg) != STATUS_OK) {
                continue;
            }

            // Now server expects data
            printf("Enter data (%d bytes): ", size);
            fflush(stdout);

            char *buffer = malloc(size);
            if (!buffer) { printf("Memory error.\n"); continue; }

            fgets(buffer, size + 1, stdin);
            sendAll(sock, buffer, size);
            free(buffer);

            // Server final response
            ProtocolResponse r;
            receiveResponse(sock, &r);
            continue;
        }

        // ======================
        // UPLOAD
        // ======================
        if (strcmp(t[0], "upload") == 0) {
            if (n < 3) {
                printf("Usage: upload <local> <remote>\n");
                continue;
            }
            clientUpload(sock, t[1], t[2]);
            continue;
        }

        // ======================
        // DOWNLOAD
        // ======================
        if (strcmp(t[0], "download") == 0) {
            if (n < 3) {
                printf("Usage: download <remote> <local>\n");
                continue;
            }
            clientDownload(sock, t[1], t[2]);
            continue;
        }

        // UNKNOWN COMMAND
        printf("Unknown command. Type 'help'.\n");
    }

    close(sock);
    return 0;
}
