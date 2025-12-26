#include <stdio.h>      // Standard I/O
#include <string.h>     // String handling
#include <stdlib.h>     // Memory management
#include <unistd.h>     // POSIX API
#include <poll.h>       // poll()
#include <sys/socket.h> // Sockets
#include <signal.h>     // Signals
#include <sys/wait.h>   // waitpid()
#include <errno.h>      // errno
#include <arpa/inet.h>  // inet_pton()

#include "../../include/network.h"        // Networking helpers
#include "../../include/utils.h"          // Utility helpers
#include "../../include/clientCommands.h" // Client command logic

#define INPUT_SIZE 512   // Maximum input length

// ANSI colors for UI
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"

// External client state accessors
extern const char* getCurrentPath();
extern const char* getUsername();
extern void unregisterBackgroundProcess(pid_t pid);

// ============================================
// SIGCHLD handler
// Cleans up finished background processes
// ============================================
static void handleChildSignal(int sig)
{
    (void)sig; // Unused parameter
    pid_t pid;
    int status;

    // Reap all terminated child processes
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        unregisterBackgroundProcess(pid);
    }
}

// ============================================
// Print interactive prompt with colors
// ============================================
static void printPrompt(void)
{
    const char *username = getUsername();
    const char *path = getCurrentPath();

    // Guest prompt
    if (username[0] == '\0') {
        printf(RED "guest" RESET "@" BLUE "127.0.0.1" RESET ":" GREEN "%s" RESET "$ ", path);
    } else {
        // Logged-in user prompt
        printf(GREEN "%s" RESET "@" BLUE "127.0.0.1" RESET ":" CYAN "%s" RESET "$ ", username, path);
    }

    fflush(stdout);
}

// ============================================
// Print client startup information
// ============================================
static void printClientInfo(const char *ip, int port)
{
    printf("\n" CYAN "============================================================\n");
    printf("                   FILE SERVER CLIENT\n");
    printf("------------------------------------------------------------\n" RESET);
    printf("Server: %s:%d\n", ip, port);
    printf(CYAN "============================================================\n\n" RESET);
}

// ============================================
// Print help message
// ============================================
void printHelp()
{
    printf("\n" YELLOW "COMMANDS:\n" RESET);
    printf("  " GREEN "login" RESET " " CYAN "<username>" RESET "                      - Login to server\n");
    printf("  " GREEN "create_user" RESET " " CYAN "<user> <perm>" RESET "             - Create user\n");
    printf("  " GREEN "delete_user" RESET " " CYAN "<username>" RESET "                - Delete user\n");
    printf("  " GREEN "cd" RESET " " CYAN "<directory>" RESET "                        - Change directory\n");
    printf("  " GREEN "list" RESET " " CYAN "[path]" RESET "                           - List directory\n");
    printf("  " GREEN "create" RESET " " CYAN "<path> <perm>" RESET " " YELLOW "[-d]" RESET "             - Create file/directory\n");
    printf("  " GREEN "chmod" RESET " " CYAN "<path> <permissions>" RESET "            - Change permissions\n");
    printf("  " GREEN "move" RESET " " CYAN "<src> <dst>" RESET "                      - Move/rename\n");
    printf("  " GREEN "delete" RESET " " CYAN "<path>" RESET "                         - Delete\n");
    printf("  " GREEN "read" RESET " " YELLOW "[-offset=N]" RESET " " CYAN "<path>" RESET "               - Read file\n");
    printf("  " GREEN "write" RESET " " YELLOW "[-offset=N]" RESET " " CYAN "<path>" RESET "              - Write to file\n");
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
    // Install SIGCHLD handler
    // Used to clean up finished background processes
    // ================================
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handleChildSignal;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    // ================================
    // Default values and safe argument parsing
    // Usage:
    //   ./client
    //   ./client <ip> <port>
    // ================================
    const char *ip = "127.0.0.1";
    int port = 8080;

    if (argc == 1) {
        // Use default IP and port
    }
    else if (argc == 3) {
        // Validate IP address
        struct in_addr tmp;
        if (inet_pton(AF_INET, argv[1], &tmp) != 1) {
            printf(RED "[X] Invalid IP address: %s\n" RESET, argv[1]);
            printf(YELLOW "[!] Usage: ./client [<ip> <port>]\n" RESET);
            return 1;
        }

        // Validate port number
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
        // Invalid argument count
        printf(RED "[X] Invalid arguments\n" RESET);
        printf(YELLOW "[!] Usage: ./client [<ip> <port>]\n" RESET);
        return 1;
    }

    // Store server info for background operations
    setGlobalServerInfo(ip, port);

    // ================================
    // Connect to server
    // ================================
    int sock = connectToServer(ip, port);
    if (sock < 0) {
        printf(RED "Could not connect to server.\n" RESET);
        return 1;
    }

    // Print startup info
    printClientInfo(ip, port);
    printf("Connected to " GREEN "%s:%d" RESET "\n", ip, port);
    printf("Type " YELLOW "'help'" RESET " for commands\n\n");

    char input[INPUT_SIZE];

    // ================================
    // poll() setup: stdin + server socket
    // ================================
    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO; // User input
    fds[0].events = POLLIN;
    fds[1].fd = sock;        // Server socket
    fds[1].events = POLLIN;

    // ================================
    // Main event loop
    // ================================
    while (1) {
        printPrompt();

        // Wait for input from user or server
        int ret = poll(fds, 2, -1);

        if (ret < 0) {
            if (errno == EINTR) continue; // Interrupted by signal
            perror("poll");
            break;
        }

        // Server closed connection or error occurred
        if (fds[1].revents & (POLLHUP | POLLERR)) {
            printf(RED "\nServer disconnected\n" RESET);
            break;
        }

        // Server activity check (detect disconnect)
        if (fds[1].revents & POLLIN) {
            char buf[1];
            int r = recv(sock, buf, 1, MSG_PEEK);
            if (r <= 0) {
                printf(RED "\nLost connection to server\n" RESET);
                break;
            }
        }

        // User entered a command
        if (fds[0].revents & POLLIN) {
            if (!fgets(input, INPUT_SIZE, stdin))
                break;

            removeNewline(input);

            // Built-in help command
            if (strcmp(input, "help") == 0) {
                printHelp();
                continue;
            }

            // Handle client command
            int exitFlag = clientHandleInput(sock, input);

            // Exit requested
            if (exitFlag == 1)
                break;
        }
    }

    // Cleanup and exit
    close(sock);
    return 0;
}
