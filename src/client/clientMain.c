#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>     // REQUIRED for close()

#include "../../include/network.h"
#include "../../include/utils.h"
#include "../../include/clientCommands.h"

#define INPUT_SIZE 512

void printHelp()
{
    printf("\n==== Available Commands ====\n");
    printf("login <username>\n");
    printf("create_user <username> <permissions>\n");
    printf("delete_user <username>\n");
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

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Usage: %s <IP> <port>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int sock = connectToServer(ip, port);
    if (sock < 0) {
        printf("Could not connect to server.\n");
        return 1;
    }

    printf("Connected to server %s:%d\n", ip, port);
    printf("Type 'help' for commands.\n");

    char input[INPUT_SIZE];

    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(input, INPUT_SIZE, stdin))
            break;

        removeNewline(input);

        if (strcmp(input, "help") == 0) {
            printHelp();
            continue;
        }

        int exitFlag = clientHandleInput(sock, input);

        if (exitFlag == 1)
            break;
    }

    close(sock);
    return 0;
}
