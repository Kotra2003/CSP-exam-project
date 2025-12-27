#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <grp.h>       // getgrnam
#include <arpa/inet.h> // inet_pton, struct in_addr

#include "../../include/network.h"
#include "../../include/protocol.h"
#include "../../include/serverCommands.h"
#include "../../include/session.h"

// Global root directory, used by other modules (fsOps, session, ...)
const char *gRootDir = NULL;

// ---------------------------------------------
// GLOBALS FOR CHILD MANAGEMENT AND SHUTDOWN
// ---------------------------------------------
#define MAX_CHILDREN 1024
#define STATUS_FORCE_LOGOUT 99

static pid_t children[MAX_CHILDREN];
static int   childCount = 0;

static volatile sig_atomic_t shutdownRequested = 0;

// ------------------------------------------------------------
// SIGCHLD handler — reap zombie processes
// ------------------------------------------------------------
static void handleChildSignal(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

// ------------------------------------------------------------
// SIGTERM / SIGINT handler — request server shutdown
// ------------------------------------------------------------
static void handleShutdownSignal(int sig)
{
    (void)sig;
    shutdownRequested = 1;
}

// ------------------------------------------------------------
// Check if rootDir is a dangerous system path
// ------------------------------------------------------------
static int isDangerousRoot(const char *dir)
{
    return (
        strcmp(dir, "/") == 0      ||
        strcmp(dir, "/root") == 0  ||
        strcmp(dir, "/etc") == 0   ||
        strcmp(dir, "/home") == 0  ||
        strcmp(dir, "/usr") == 0
    );
}

// ------------------------------------------------------------
// Ensure root directory exists (or create it)
// ------------------------------------------------------------
static int ensureRootDirectory(const char *path)
{
    struct stat st;

    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "ERROR: '%s' exists but is not a directory.\n", path);
            return -1;
        }
        return 0;
    }

    if (mkdir(path, 0755) < 0) {
        perror("mkdir");
        return -1;
    }

    printf("[INFO] Created root directory '%s'\n", path);
    return 0;
}

// ------------------------------------------------------------
// Check and create csapgroup group if it does not exist
// ------------------------------------------------------------
static int ensureCsapGroupExists(void)
{
    struct group *grp = getgrnam("csapgroup");
    if (grp != NULL) {
        printf("[INFO] Group 'csapgroup' exists (GID=%d)\n",
               (int)grp->gr_gid);
        return 0;
    }

    // Create group if missing (requires sudo)
    printf("[INFO] Creating group 'csapgroup'...\n");

    pid_t pid = fork();
    if (pid == 0) {
        execlp("groupadd", "groupadd", "csapgroup", NULL);
        perror("execlp groupadd");
        _exit(1);
    }

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf("[INFO] Group 'csapgroup' created successfully\n");
        return 0;
    } else {
        printf("[WARNING] Failed to create group 'csapgroup'. "
               "User creation will fail.\n");
        return -1;
    }
}

// ------------------------------------------------------------
// Server banner
// ------------------------------------------------------------
static void printBanner(const char *root, const char *ip, int port)
{
    printf("\n");
    printf("============================================================\n");
    printf("                        FILE SERVER\n");
    printf("------------------------------------------------------------\n");
    printf("              - Root Directory : %s\n", root);
    printf("              - Listening on   : %s:%d\n", ip, port);
    printf("============================================================\n\n");
    fflush(stdout);
}

// ------------------------------------------------------------
// Process that watches server console (STDIN)
// ------------------------------------------------------------
static void runConsoleWatcher(pid_t parentPid)
{
    char line[256];

    printf("[CONSOLE] Type 'exit' or CTRL+C to stop the server.\n");
    fflush(stdout);

    while (fgets(line, sizeof(line), stdin) != NULL) {
        //We need this becase exit and exit\n is not the same
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        if (strcmp(line, "exit") == 0) {
            printf("[CONSOLE] Shutdown requested...\n");
            fflush(stdout);
            kill(parentPid, SIGTERM);
            break;
        }
    }

    _exit(0);
}

// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------
int main(int argc, char *argv[])
{
    // Disable umask restrictions (permissions are explicit)
    // I had a problem with write and create so I needed to add it
    umask(000);

    // =====================================================
    // FLEXIBLE ARGUMENT PARSING:
    // ./server <root_directory> [<IP>] [<port>]
    // Default: 127.0.0.1:8080
    // =====================================================

    // Default values
    const char *rootDir = NULL;
    const char *ip = "127.0.0.1";
    int port = 8080;

    // At least root directory is required
    if (argc < 2) {
        fprintf(stderr,
                "Usage: %s <root_directory> [<IP>] [<port>]\n", argv[0]);
        fprintf(stderr, "Examples:\n");
        fprintf(stderr,
                "  %s /root_direcotry           "
                "(default: 127.0.0.1:8080)\n", argv[0]);
        fprintf(stderr,
                "  %s /root_direcotry 192.168.1.100\n", argv[0]);
        fprintf(stderr,
                "  %s /root_direcotry 0.0.0.0 9090\n", argv[0]);
        return 1;
    }

        rootDir = argv[1];

    // If there are 4 or more arguments, the third one is IP and the fourth is port
    if (argc >= 4) {
        port = atoi(argv[3]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr,
                    "ERROR: Invalid port number: %s (must be 1-65535)\n",
                    argv[3]);
            return 1;
        }
        ip = argv[2]; // Second argument is IP
    }
    // If there are exactly 3 arguments
    else if (argc == 3) {
        // Check whether the second argument is IP or port
        // If it consists only of digits, assume it is a port
        int allDigits = 1;
        for (int i = 0; argv[2][i] != '\0'; i++) {
            if (!isdigit((unsigned char)argv[2][i])) {
                allDigits = 0;
                break;
            }
        }

        if (allDigits) {
            // Second argument is port, IP remains default
            port = atoi(argv[2]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr,
                        "ERROR: Invalid port number: %s\n",
                        argv[2]);
                return 1;
            }
        } else {
            // Second argument is IP, port remains default
            ip = argv[2];
        }
    }
    // argc == 2: use default IP and port

    // Safety check: forbid dangerous system paths as root directory
    if (isDangerousRoot(rootDir)) {
        fprintf(stderr,
                "ERROR: Root directory '%s' is a critical system path.\n",
                rootDir);
        return 1;
    }

    // Set global root directory
    gRootDir = rootDir;

    // Ensure root directory exists (create if missing)
    if (ensureRootDirectory(rootDir) < 0) {
        return 1;
    }

    // Ensure csapgroup exists (create if missing)
    ensureCsapGroupExists();

    // -----------------------------------------------------
    // Signal handlers
    // -----------------------------------------------------
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handleChildSignal;
    sa.sa_flags   = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    struct sigaction saTerm;
    memset(&saTerm, 0, sizeof(saTerm));
    saTerm.sa_handler = handleShutdownSignal;
    sigaction(SIGTERM, &saTerm, NULL);

    struct sigaction saInt;
    memset(&saInt, 0, sizeof(saInt));
    saInt.sa_handler = handleShutdownSignal;
    sigaction(SIGINT, &saInt, NULL);

    // =====================================================
    // FIX: Validate IP before createServerSocket()
    // inet_pton returns 0 for invalid IP and does NOT set errno.
    // Therefore perror must NOT be used for return == 0.
    // =====================================================
    {
        struct in_addr tmp;
        int ok = inet_pton(AF_INET, ip, &tmp);

        if (ok == 0) {
            fprintf(stderr,
                    "FATAL: Invalid IPv4 address: %s\n",
                    ip);
            return 1;
        }
        if (ok < 0) {
            perror("inet_pton");
            return 1;
        }
    }

    // Create server socket
    // Just used to wait for clients
    int serverFd = createServerSocket(ip, port);
    if (serverFd < 0) {
        fprintf(stderr,
                "FATAL: Could not bind to %s:%d\n",
                ip, port);
        return 1;
    }

    // -----------------------------------------------------
    // Drop root privileges after binding
    // -----------------------------------------------------
    uid_t target_uid;
    gid_t target_gid;

    char *su = getenv("SUDO_UID");
    char *sg = getenv("SUDO_GID");

    if (su && sg) {
        target_uid = (uid_t)atoi(su);
        target_gid = (gid_t)atoi(sg);
    } else {
        // Not started via sudo
        target_uid = getuid();
        target_gid = getgid();
    }

    if (geteuid() == 0) {
        if (setegid(target_gid) != 0) {
            perror("setegid");
            exit(1);
        }
        if (seteuid(target_uid) != 0) {
            perror("seteuid");
            exit(1);
        }
    }

    printf("[SECURITY] Runtime ruid=%d euid=%d target=%d\n",
           getuid(), geteuid(), target_uid);

    // Print server banner
    printBanner(rootDir, ip, port);

    // -----------------------------------------------------
    // Console watcher process
    // -----------------------------------------------------
    pid_t consolePid = fork();
    if (consolePid == 0) {
        runConsoleWatcher(getppid());
    }

    // =====================================================
    // POLL-BASED ACCEPT LOOP
    // =====================================================
    struct pollfd pfds[1];
    pfds[0].fd = serverFd;
    pfds[0].events = POLLIN;

    int pollTimeout = 1000; // 1 second (milliseconds)

    // Accept loop using poll()
    while (!shutdownRequested) {
        int pollRet = poll(pfds, 1, pollTimeout);

        if (pollRet < 0) {
            if (errno == EINTR)
                continue; // Interrupted by signal
            perror("poll");
            break;
        }

        if (pollRet == 0) {
            // Timeout: just re-check shutdown flag
            continue;
        }

        // Incoming connection
        if (pfds[0].revents & POLLIN) {
            int clientFd = acceptClient(serverFd);

            if (shutdownRequested) {
                if (clientFd >= 0)
                    close(clientFd);
                break;
            }

            if (clientFd < 0) {
                if (errno == EINTR)
                    continue;
                perror("acceptClient");
                continue;
            }

            pid_t pid = fork();

            if (pid == 0) {
                // Child: handle client
                close(serverFd);

                Session session;
                initSession(&session);

                ProtocolMessage msg;
                while (1) {
                    if (receiveMessage(clientFd, &msg) < 0) {
                        printf("[INFO] Client disconnected.\n");
                        break;
                    }

                    if (msg.command == CMD_EXIT) {
                        ProtocolResponse ok = { STATUS_OK, 0 };
                        sendResponse(clientFd, &ok);
                        break;
                    }

                    processCommand(clientFd, &msg, &session);
                }

                close(clientFd);
                _exit(0);
            }

            // Parent: track child and close client FD
            if (childCount < MAX_CHILDREN)
                children[childCount++] = pid;

            close(clientFd);
        }
    }
    // =====================================================

    printf("\n[SHUTDOWN] Server shutting down...\n");
    close(serverFd);
    kill(consolePid, SIGKILL);

    // Terminate all active client handlers
    for (int i = 0; i < childCount; i++) {
        if (children[i] > 0) {
            kill(children[i], SIGTERM);
        }
    }

    // Wait for all children to exit
    while (waitpid(-1, NULL, 0) > 0) {}

    printf("[SHUTDOWN] All client handlers terminated.\n");
    printf("[SHUTDOWN] Server terminated cleanly.\n");

    return 0;
}
