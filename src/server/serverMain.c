#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <bits/waitflags.h>

#include "../../include/network.h"
#include "../../include/protocol.h"
#include "../../include/concurrency.h"


// Handle SIGCHLD to avoid zombie processes
void handleChild(int sig)
{
    // Wait for any child process (non-blocking)
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

const char *gRootDir = NULL;

int main(int argc, char *argv[])
{
    // Check arguments count
    if (argc < 2) {
        printf("Usage: %s <root_directory> [IP] [port]\n", argv[0]);
        return 1;
    }

    // Root directory for server
    const char *rootDir = argv[1];

    // Default IP and port
    const char *ip = (argc >= 3) ? argv[2] : "127.0.0.1";
    int port        = (argc >= 4) ? atoi(argv[3]) : 8080;

    // Create root directory if it does not exist
    struct stat st;
    if (stat(rootDir, &st) < 0) {
        // Try to create it
        if (mkdir(rootDir, 0755) < 0) {
            perror("mkdir root");
            return 1;
        }
        printf("Root directory %s created.\n", rootDir);
    }

    gRootDir = rootDir;

    // Install SIGCHLD handler to clean up child processes
    signal(SIGCHLD, handleChild);

    // Create server listening socket
    int serverFd = createServerSocket(ip, port);
    if (serverFd < 0) {
        printf("Failed to create server socket.\n");
        return 1;
    }

    printf("Server running on %s:%d\n", ip, port);

    // Main accept loop
    while (1)
    {
        int clientFd = acceptClient(serverFd);
        if (clientFd < 0) {
            perror("acceptClient");
            continue;
        }

        // Create child process to handle the client
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            close(clientFd);
            continue;
        }

        if (pid == 0) {
            // Child: handle client requests
            close(serverFd);

            ProtocolMessage msg;
            ProtocolResponse res;

            // Simple loop until exit command
            while (1) {
                // Receive a message from client
                if (receiveMessage(clientFd, &msg) < 0) {
                    printf("Client disconnected.\n");
                    break;
                }

                // If client wants to exit
                if (msg.command == CMD_EXIT) {
                    res.status = STATUS_OK;
                    res.dataSize = 0;
                    sendResponse(clientFd, &res);
                    break;
                }

                // For default message we always return STATUS_OK
                res.status = STATUS_OK;
                res.dataSize = 0;
                sendResponse(clientFd, &res);
            }

            close(clientFd);
            exit(0);
        }

        // Parent: close client socket and continue
        close(clientFd);
    }

    return 0;
}
