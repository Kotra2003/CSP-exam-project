#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>       
#include <sys/socket.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#include "../../include/network.h"
#include "../../include/utils.h"
#include "../../include/clientCommands.h"

#define INPUT_SIZE 512

// EXTERN deklaracije
extern const char* getCurrentPath();
extern const char* getUsername();  // DODAJ
extern void unregisterBackgroundProcess(pid_t pid);

// ============================================
// SIGCHLD HANDLER
// ============================================
static void handleChildSignal(int sig)
{
    (void)sig;
    pid_t pid;
    int status;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[Background] Process %d finished\n", pid);
        unregisterBackgroundProcess(pid);
    }
}

// ============================================
// PRINT PROMPT (lijep format)
// ============================================
static void printPrompt(void)
{
    const char *username = getUsername();
    const char *path = getCurrentPath();
    
    if (username[0] == '\0') {
        // Nije logovan - guest mode
        printf("guest@127.0.0.1:%s$ ", path);
    } else {
        // Logovan korisnik
        printf("%s@127.0.0.1:%s$ ", username, path);
    }
    
    fflush(stdout);
}

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
    // ================================
    // POSTAVI SIGNAL HANDLERE
    // ================================
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handleChildSignal;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    
    // ================================
    // DEFAULTNE VRIJEDNOSTI
    // ================================
    const char *ip = "127.0.0.1";
    int port = 8080;

    if (argc == 3) {
        ip = argv[1];
        port = atoi(argv[2]);
    }

    setGlobalServerInfo(ip, port);

    int sock = connectToServer(ip, port);
    if (sock < 0) {
        printf("Could not connect to server.\n");
        return 1;
    }

    printf("Connected to server %s:%d\n", ip, port);
    printf("Type 'help' for commands.\n\n");

    char input[INPUT_SIZE];

    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sock;
    fds[1].events = POLLIN;

    while (1) {
        printPrompt();

        int ret = poll(fds, 2, -1);

        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        // Server se ugasio
        if (fds[1].revents & (POLLHUP | POLLERR)) {
            printf("\n[INFO] Server has shut down. Closing client...\n");
            break;
        }

        // Server poslao nešto
        if (fds[1].revents & POLLIN) {
            char buf[1];
            int r = recv(sock, buf, 1, MSG_PEEK);
            if (r <= 0) {
                printf("\n[INFO] Lost connection to server. Closing client...\n");
                break;
            }
        }

        // User input
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