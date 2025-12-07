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

// ------------------------ avoid zombie processes ------------------------
void handleChild(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s <root_directory> [IP] [port]\n", argv[0]);
        return 1;
    }

    const char *rootDir = argv[1];
    const char *ip      = (argc >= 3) ? argv[2] : "127.0.0.1";
    int port            = (argc >= 4) ? atoi(argv[3]) : 8080;

    // Create root directory if missing
    struct stat st;
    if (stat(rootDir, &st) < 0) {
        if (mkdir(rootDir, 0755) < 0) {
            perror("mkdir root");
            return 1;
        }
        printf("Root directory %s created.\n", rootDir);
    }

    gRootDir = rootDir;

    signal(SIGCHLD, handleChild);

    int serverFd = createServerSocket(ip, port);
    if (serverFd < 0) {
        printf("Failed to create server socket.\n");
        return 1;
    }

    printf("Server running on %s:%d\n", ip, port);

    while (1)
    {
        int clientFd = acceptClient(serverFd);
        if (clientFd < 0) {
            perror("acceptClient");
            continue;
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            close(clientFd);
            continue;
        }

        if (pid == 0)
        {
            // ------------------------ CHILD PROCESS ------------------------
            close(serverFd);

            ProtocolMessage msg;
            Session session;
            memset(&session, 0, sizeof(Session));
            session.isLoggedIn = 0;

            while (1)
            {
                if (receiveMessage(clientFd, &msg) < 0) {
                    printf("Client disconnected.\n");
                    break;
                }

                // If EXIT we need to close client (child process)
                if (msg.command == CMD_EXIT) {
                    ProtocolResponse res;
                    res.status = STATUS_OK;
                    res.dataSize = 0;
                    sendResponse(clientFd, &res);
                    break;
                }

                // CALL THE ACTUAL COMMAND HANDLER!
                processCommand(clientFd, &msg, &session);
            }

            close(clientFd);
            exit(0);
        }

        // ------------------------ PARENT PROCESS ------------------------
        close(clientFd);
    }

    return 0;
}
