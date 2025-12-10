#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>       // <-- OVO JE NOVO
#include <sys/socket.h>   // <-- OVO DODAJ

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
    printf("cd <directory>\n");
    printf("list [directory]\n");
    printf("create <path> <permissions> [-d]\n");
    printf("chmod <path> <permissions>\n");
    printf("move <src> <dst>\n");
    printf("delete <path>\n");
    printf("read [-offset=N] <path>\n");
    printf("write [-offset=N] <path>\n");
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

    setGlobalServerInfo(ip, port);

    int sock = connectToServer(ip, port);
    if (sock < 0) {
        printf("Could not connect to server.\n");
        return 1;
    }

    printf("Connected to server %s:%d\n", ip, port);
    printf("Type 'help' for commands.\n");

    char input[INPUT_SIZE];

    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;   // keyboard
    fds[0].events = POLLIN;

    fds[1].fd = sock;           // server socket
    fds[1].events = POLLIN;

    while (1) {

        printf("> ");
        fflush(stdout);

        int ret = poll(fds, 2, -1);   // blokira dok se bilo šta ne desi

        if (ret < 0) {
            perror("poll");
            break;
        }

        // ---------------------------------------------
        // CASE 1: Server se ugasio → socket zatvoren
        // ---------------------------------------------
        if (fds[1].revents & POLLHUP || fds[1].revents & POLLERR) {
            printf("\n[INFO] Server has shut down. Closing client...\n");
            break;
        }

        // ---------------------------------------------
        // CASE 2: Server poslao nešto (ne bi trebao osim odgovora)
        // Ako recv vrati 0 → server mrtav
        // ---------------------------------------------
        if (fds[1].revents & POLLIN) {
            char buf[1];
            int r = recv(sock, buf, 1, MSG_PEEK);
            if (r <= 0) {
                printf("\n[INFO] Lost connection to server. Closing client...\n");
                break;
            }
        }

        // ---------------------------------------------
        // CASE 3: User je nešto otkucao na tastaturi
        // ---------------------------------------------
        if (fds[0].revents & POLLIN) {
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

            continue;
        }
    }

    close(sock);
    return 0;
}
