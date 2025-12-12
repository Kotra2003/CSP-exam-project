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
#include <poll.h>
#include <grp.h>  // Dodano za getgrnam

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
// Provjeri i kreiraj csapgroup grupu ako ne postoji
// ------------------------------------------------------------
static int ensureCsapGroupExists(void)
{
    struct group *grp = getgrnam("csapgroup");
    if (grp != NULL) {
        printf("[INFO] Group 'csapgroup' exists (GID=%d)\n", (int)grp->gr_gid);
        return 0;
    }

    // Kreiraj grupu ako ne postoji (potrebno sudo)
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
        printf("[WARNING] Failed to create group 'csapgroup'. User creation will fail.\n");
        return -1;
    }
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
    // FLEKSIBILNO PARSIRANJE ARGUMENATA:
    // ./server <root_directory> [<IP>] [<port>]
    // Default: 127.0.0.1:8080
    // =====================================================
    
    // Default vrednosti
    const char *rootDir = NULL;
    const char *ip = "127.0.0.1";
    int port = 8080;
    
    // Minimalno: 1 argument (root_dir)
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <root_directory> [<IP>] [<port>]\n", argv[0]);
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s /var/myroot           (default: 127.0.0.1:8080)\n", argv[0]);
        fprintf(stderr, "  %s /var/myroot 192.168.1.100\n", argv[0]);
        fprintf(stderr, "  %s /var/myroot 0.0.0.0 9090\n", argv[0]);
        return 1;
    }
    
    rootDir = argv[1];
    
    // Ako ima 3 ili više argumenata, treći je port
    if (argc >= 4) {
        port = atoi(argv[3]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "ERROR: Invalid port number: %s (must be 1-65535)\n", argv[3]);
            return 1;
        }
        ip = argv[2]; // Drugi argument je IP
    }
    // Ako ima tačno 3 argumenta
    else if (argc == 3) {
        // Provjeri da li je drugi argument IP ili port
        // Ako je sve brojevi, možda je port
        int allDigits = 1;
        for (int i = 0; argv[2][i] != '\0'; i++) {
            if (!isdigit(argv[2][i])) {
                allDigits = 0;
                break;
            }
        }
        
        if (allDigits) {
            // Drugi argument je port, IP ostaje default
            port = atoi(argv[2]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "ERROR: Invalid port number: %s\n", argv[2]);
                return 1;
            }
        } else {
            // Drugi argument je IP, port ostaje default
            ip = argv[2];
        }
    }
    // argc == 2: koristi default IP i port

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

    // Provjeri/kreiraj csapgroup
    ensureCsapGroupExists();

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

    // =====================================================
    // POLL IMPLEMENTACIJA
    // =====================================================
    struct pollfd pfds[1];
    pfds[0].fd = serverFd;
    pfds[0].events = POLLIN;
    
    int pollTimeout = 1000; // 1 sekund (u milisekundama)

    // Accept loop sa poll()
    while (!shutdownRequested) {
        int pollRet = poll(pfds, 1, pollTimeout);
        
        if (pollRet < 0) {
            if (errno == EINTR) continue; // Signal primljen
            perror("poll");
            break;
        }
        
        if (pollRet == 0) {
            // Timeout - proveri da li je shutdown zahtevan
            continue;
        }
        
        // Ima nova konekcija
        if (pfds[0].revents & POLLIN) {
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
    }
    // =====================================================

    printf("\n[SHUTDOWN] Server shutting down...\n");
    close(serverFd);
    kill(consolePid, SIGKILL);

    // Ubij sve klijente koji su još živi
    for (int i = 0; i < childCount; i++) {
        if (children[i] > 0) {
            kill(children[i], SIGTERM);
        }
    }
    
    // Sačekaj da sva deca završe
    while (waitpid(-1, NULL, 0) > 0) {}

    printf("[SHUTDOWN] All client handlers terminated.\n");
    printf("[SHUTDOWN] Server terminated cleanly.\n");

    return 0;
}