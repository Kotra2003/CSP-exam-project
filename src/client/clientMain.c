// src/client/clientMain.c

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "../../include/network.h"
#include "../../include/utils.h"
#include "../../include/clientCommands.h"

#define INPUT_SIZE 512

int main(int argc, char *argv[])
{
    // -----------------------------------
    // DEFAULT IP + PORT
    // -----------------------------------
    const char *ip;
    int port;

    if (argc == 1) {
        ip = "127.0.0.1";
        port = 8080;
        printf("[INFO] Using default server %s:%d\n", ip, port);
    }
    else if (argc == 3) {
        ip   = argv[1];
        port = atoi(argv[2]);
    }
    else {
        printf("Usage:\n");
        printf("  %s                (default 127.0.0.1 8080)\n", argv[0]);
        printf("  %s <IP> <port>\n", argv[0]);
        return 1;
    }

    setGlobalServerInfo(ip, port);

    int sock = connectToServer(ip, port);
    if (sock < 0) {
        printf("Could not connect to server.\n");
        return 1;
    }

    printf("Connected to %s:%d\n", ip, port);

    char input[INPUT_SIZE];
    struct pollfd fds[2] = {
        { STDIN_FILENO, POLLIN, 0 },
        { sock,          POLLIN, 0 },
    };

    while (1) {
        // Background cleanup
        int status;
        pid_t p;
        while ((p = waitpid(-1, &status, WNOHANG)) > 0)
            unregisterBackgroundProcess(p);

        printf("> ");
        fflush(stdout);

        if (poll(fds, 2, -1) < 0) break;

        // Server died
        if (fds[1].revents & (POLLHUP | POLLERR)) {
            printf("\n[INFO] Server closed.\n");
            break;
        }

        // Input
        if (fds[0].revents & POLLIN) {
            if (!fgets(input, sizeof(input), stdin))
                break;

            removeNewline(input);

            if (strcmp(input, "help") == 0) {
                printHelp();
                continue;
            }

            if (clientHandleInput(sock, input) == 1)
                break;
        }
    }

    close(sock);
    return 0;
}

void printHelp() {
    printf("\nCommands:\n");
    printf(" login <user>\n create_user <u> <perm>\n delete_user <u>\n");
    printf(" cd <dir>\n list [dir]\n create <p> <perm> [-d]\n chmod <p> <perm>\n");
    printf(" move <s> <d>\n delete <p>\n read [-offset=N] <p>\n write [-offset=N] <p>\n");
    printf(" upload <local> <remote>\n download <remote> <local>\n exit\n\n");
}
