#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>       
#include <sys/socket.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <arpa/inet.h>

#include "../../include/network.h"
#include "../../include/utils.h"
#include "../../include/clientCommands.h"

#define INPUT_SIZE 512

// Boje za UI
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"

// EXTERN deklaracije
extern const char* getCurrentPath();
extern const char* getUsername();
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
        unregisterBackgroundProcess(pid);
    }
}

// ============================================
// PRINT PROMPT (sa bojama)
// ============================================
static void printPrompt(void)
{
    const char *username = getUsername();
    const char *path = getCurrentPath();
    
    if (username[0] == '\0') {
        // Nije logovan - guest mode
        printf(RED "guest" RESET "@" BLUE "127.0.0.1" RESET ":" GREEN "%s" RESET "$ ", path);
    } else {
        // Logovan korisnik
        printf(GREEN "%s" RESET "@" BLUE "127.0.0.1" RESET ":" CYAN "%s" RESET "$ ", username, path);
    }
    
    fflush(stdout);
}

// ============================================
// PRINT CLIENT INFO
// ============================================
static void printClientInfo(const char *ip, int port)
{
    printf("\n" CYAN "============================================================\n");
    printf("                   FILE SERVER CLIENT\n");
    printf("------------------------------------------------------------\n" RESET);
    printf("Server: %s:%d\n", ip, port);
    printf(CYAN "============================================================\n\n" RESET);
}

void printHelp()
{
    printf("\n" YELLOW "COMMANDS:\n" RESET);
    printf("  " GREEN "login" RESET " " CYAN "<username>" RESET "                      - Login to server\n");
    printf("  " GREEN "create_user" RESET " " CYAN "<user> <perm>" RESET "             - " YELLOW "Create user" RESET "\n");
    printf("  " GREEN "delete_user" RESET " " CYAN "<username>" RESET "                - " RED "Delete user" RESET "\n");
    printf("  " GREEN "cd" RESET " " CYAN "<directory>" RESET "                        - Change directory\n");
    printf("  " GREEN "list" RESET " " CYAN "[path]" RESET "                           - List directory\n");
    printf("  " GREEN "create" RESET " " CYAN "<path> <perm>" RESET " " YELLOW "[-d]" RESET "             - Create file/directory " YELLOW "(with -d)" RESET "\n");
    printf("  " GREEN "chmod" RESET " " CYAN "<path> <permissions>" RESET "            - Change permissions\n");
    printf("  " GREEN "move" RESET " " CYAN "<src> <dst>" RESET "                      - Move/rename\n");
    printf("  " GREEN "delete" RESET " " CYAN "<path>" RESET "                         - Delete\n");
    printf("  " GREEN "read" RESET " " YELLOW "[-offset=N]" RESET " " CYAN "<path>" RESET "               - Read file " YELLOW "(with offset)" RESET "\n");
    printf("  " GREEN "write" RESET " " YELLOW "[-offset=N]" RESET " " CYAN "<path>" RESET "              - Write to file " YELLOW "(with offset)" RESET "\n");
    printf("  " GREEN "upload" RESET " " YELLOW "[-b]" RESET " " CYAN "<local> <remote>" RESET "          - Upload\n");
    printf("  " GREEN "download" RESET " " YELLOW "[-b]" RESET " " CYAN "<remote> <local>" RESET "        - Download\n");
    printf("  " GREEN "exit" RESET "                                  - Exit client\n");
    printf("  " GREEN "help" RESET "                                  - Show this help\n\n");
}

// ============================================
// MAIN
// ============================================
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
    // ================================
        // DEFAULTNE VRIJEDNOSTI + SAFE ARG PARSING
        // Usage:
        //   ./client
        //   ./client <ip> <port>
        // ================================
        const char *ip = "127.0.0.1";
        int port = 8080;

        if (argc == 1) {
            // ./client -> defaults
        }
        else if (argc == 3) {
            // ./client <ip> <port>

            // Validate IP (avoid inet_pton: Success noise)
            struct in_addr tmp;
            if (inet_pton(AF_INET, argv[1], &tmp) != 1) {
                printf(RED "[X] Invalid IP address: %s\n" RESET, argv[1]);
                printf(YELLOW "[!] Usage: ./client [<ip> <port>]\n" RESET);
                return 1;
            }

            // Validate port
            char *endptr = NULL;
            long p = strtol(argv[2], &endptr, 10);
            if (!endptr || *endptr != '\0' || p <= 0 || p > 65535) {
                printf(RED "[X] Invalid port: %s\n" RESET, argv[2]);
                printf(YELLOW "[!] Port must be between 1 and 65535\n" RESET);
                return 1;
            }

            ip = argv[1];
            port = (int)p;
        }
        else {
            // Any other number of args
            printf(RED "[X] Invalid arguments\n" RESET);
            printf(YELLOW "[!] Usage: ./client [<ip> <port>]\n" RESET);
            return 1;
        }

        setGlobalServerInfo(ip, port);


    int sock = connectToServer(ip, port);
    if (sock < 0) {
        printf(RED "Could not connect to server.\n" RESET);
        return 1;
    }

    printClientInfo(ip, port);
    printf("Connected to " GREEN "%s:%d" RESET "\n", ip, port);
    printf("Type " YELLOW "'help'" RESET " for commands\n\n");

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
            printf(RED "\nServer disconnected\n" RESET);
            break;
        }

        // Server poslao nešto
        if (fds[1].revents & POLLIN) {
            char buf[1];
            int r = recv(sock, buf, 1, MSG_PEEK);
            if (r <= 0) {
                printf(RED "\nLost connection to server\n" RESET);
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