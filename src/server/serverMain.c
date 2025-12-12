// src/server/serverMain.c

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

#include "../../include/network.h"
#include "../../include/protocol.h"
#include "../../include/concurrency.h"
#include "../../include/serverCommands.h"
#include "../../include/session.h"

// Global root dir, koristi se u drugim modulima (fsOps, session, ...)
const char *gRootDir = NULL;

// ---------------------------------------------
// GLOBALS ZA UPRAVLJANJE DJECOM I SHUTDOWN-om
// ---------------------------------------------
#define MAX_CHILDREN 1024
#define STATUS_FORCE_LOGOUT 99

static pid_t children[MAX_CHILDREN];
static int   childCount = 0;

static volatile sig_atomic_t shutdownRequested = 0;

// ------------------------------------------------------------
// SIGCHLD handler — pokupi sve zombie procese
// ------------------------------------------------------------
static void handleChildSignal(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

// ------------------------------------------------------------
// SIGTERM/SIGINT handler — zahtjev za gašenje servera
// ------------------------------------------------------------
static void handleShutdownSignal(int sig)
{
    (void)sig;
    shutdownRequested = 1;
}

// ------------------------------------------------------------
// Provjera da li je rootDir kritičan sistemski put
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
// Osiguraj da root direktorij postoji (ili ga napravi)
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
// Banner
// ------------------------------------------------------------
static void printBanner(const char *root, const char *ip, int port, int sudoMode)
{
    printf("\n");
    printf("============================================================\n");
    printf("                        FILE SERVER\n");
    printf("------------------------------------------------------------\n");
    printf(" - Root Directory : %s\n", root);
    printf(" - Listening on   : %s:%d\n", ip, port);
    printf(" - Privileges     : %s\n",
           sudoMode ? "user management ENABLED" : "user management DISABLED");
    printf("============================================================\n\n");
    fflush(stdout);
}

// ------------------------------------------------------------
// Proces koji sluša STDIN server terminala
// ------------------------------------------------------------
static void runConsoleWatcher(pid_t parentPid)
{
    char line[256];

    printf("[CONSOLE] Type 'exit' or CTRL+C to stop the server.\n");
    fflush(stdout);

    while (fgets(line, sizeof(line), stdin) != NULL) {
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
    umask(000);
    // =====================================================
    // PDF ZAHTJEV: 
    // Server se pokreće SAMO kao:
    //      ./server <root_directory>
    //
    // IP i port su default:
    //      127.0.0.1 , 8080
    // =====================================================
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <root_directory>\n", argv[0]);
        return 1;
    }

    const char *rootDir = argv[1];
    const char *ip      = "127.0.0.1";
    int         port    = 8080;

    // Sigurnosna provjera
    if (isDangerousRoot(rootDir)) {
        fprintf(stderr,
                "ERROR: Root directory '%s' is a critical system path.\n",
                rootDir);
        return 1;
    }

    // Privilegije (sudo ili ne)
    uid_t ruid = getuid();
    uid_t euid = geteuid();
    int sudoMode = 0;

    if (euid == 0 && ruid != 0) {
        sudoMode = 1;
        printf("[SECURITY] Running with sudo (ruid=%d). Dropping privileges.\n", (int)ruid);

        if (seteuid(ruid) != 0) {
            perror("seteuid drop");
            return 1;
        }
    } else if (euid == 0 && ruid == 0) {
        sudoMode = 1;
    } else {
        sudoMode = 0;
        printf("[SECURITY] Running as normal user – usermgmt disabled.\n");
    }

    gRootDir = rootDir;

    if (ensureRootDirectory(rootDir) < 0) {
        return 1;
    }

    // Signal handler-i
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

    // Kreiraj server socket
    int serverFd = createServerSocket(ip, port);
    if (serverFd < 0) {
        fprintf(stderr, "FATAL: Could not bind to %s:%d\n", ip, port);
        return 1;
    }

    printBanner(rootDir, ip, port, sudoMode);

    // Console watcher proces
    pid_t consolePid = fork();
    if (consolePid == 0) {
        runConsoleWatcher(getppid());
    }

    // Accept loop
    while (!shutdownRequested) {
        int clientFd = acceptClient(serverFd);

        if (shutdownRequested) {
            if (clientFd >= 0) close(clientFd);
            break;
        }

        if (clientFd < 0) {
            if (errno == EINTR) continue;
            perror("acceptClient");
            continue;
        }

        pid_t pid = fork();

        if (pid == 0) {
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

        if (childCount < MAX_CHILDREN)
            children[childCount++] = pid;

        close(clientFd);
    }

    printf("\n[SHUTDOWN] Server shutting down...\n");
    close(serverFd);
    kill(consolePid, SIGKILL);

    while (waitpid(-1, NULL, 0) > 0) {}

    printf("[SHUTDOWN] All client handlers terminated.\n");
    printf("[SHUTDOWN] Server terminated cleanly.\n");

    return 0;
}
