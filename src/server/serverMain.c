#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "../../include/network.h"
#include "../../include/protocol.h"
#include "../../include/concurrency.h"
#include "../../include/serverCommands.h"
#include "../../include/session.h"

const char *gRootDir = NULL;

// ------------------------------------------------------------
// Reap zombie children
// ------------------------------------------------------------
static void handleChildSignal(int sig)
{
    (void)sig;

    // Reap ALL zombie children
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

// ------------------------------------------------------------
// Validate or create root directory
// ------------------------------------------------------------
static int ensureRootDirectory(const char *path)
{
    struct stat st;

    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "ERROR: '%s' exists but is not a directory.\n", path);
            return -1;
        }
        return 0; // already exists
    }

    // Create directory
    if (mkdir(path, 0755) < 0) {
        perror("mkdir root");
        return -1;
    }

    printf("Root directory '%s' created.\n", path);
    return 0;
}

// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------
int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s <root_directory> [IP] [port]\n", argv[0]);
        return 1;
    }

    const char *rootDir = argv[1];
    const char *ip      = (argc >= 3) ? argv[2] : "127.0.0.1";
    int port            = (argc >= 4) ? atoi(argv[3]) : 8080;

    // Save globally for resolvePath & others
    gRootDir = rootDir;

    // Ensure root directory exists
    if (ensureRootDirectory(rootDir) < 0)
        return 1;

    // --------------------------------------------------------
    // Handle zombie child processes (robust version)
    // --------------------------------------------------------
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handleChildSignal;
    sa.sa_flags   = SA_RESTART;   // <--- important so accept() is not interrupted

    sigaction(SIGCHLD, &sa, NULL);

    // --------------------------------------------------------
    // Create server socket
    // --------------------------------------------------------
    int serverFd = createServerSocket(ip, port);
    if (serverFd < 0) {
        fprintf(stderr, "FATAL: Could not create server socket.\n");
        return 1;
    }

    printf("Server running on %s:%d\n", ip, port);
    fflush(stdout);

    // --------------------------------------------------------
    // Accept loop
    // --------------------------------------------------------
    while (1)
    {
        int clientFd = acceptClient(serverFd);
        if (clientFd < 0) {
            perror("acceptClient");
            continue;        // try again
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            close(clientFd);
            continue;
        }

        if (pid == 0)
        {
            // ------------------ CHILD PROCESS ------------------
            close(serverFd);

            Session session;
            initSession(&session);

            ProtocolMessage msg;

            while (1)
            {
                if (receiveMessage(clientFd, &msg) < 0) {
                    printf("Client disconnected.\n");
                    break;
                }

                if (msg.command == CMD_EXIT) {
                    ProtocolResponse res = { STATUS_OK, 0 };
                    sendResponse(clientFd, &res);
                    break;
                }

                processCommand(clientFd, &msg, &session);
            }

            close(clientFd);
            exit(0);
        }

        // ------------------ PARENT PROCESS ---------------------
        close(clientFd);
    }

    return 0;
}
