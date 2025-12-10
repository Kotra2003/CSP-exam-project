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
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // samo čistimo zombije
    }
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
// Provjera da li je rootDir kritičan sistemski put (sigurnosni check)
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
        return 0;   // već postoji, sve ok
    }

    if (mkdir(path, 0755) < 0) {
        perror("mkdir");
        return -1;
    }

    printf("[INFO] Created root directory '%s'\n", path);
    return 0;
}

// ------------------------------------------------------------
// Validacija porta (mora biti 1–65535)
// ------------------------------------------------------------
static int validatePort(const char *p)
{
    for (int i = 0; p[i]; i++) {
        if (!isdigit((unsigned char)p[i])) {
            return -1;
        }
    }

    int val = atoi(p);
    if (val < 1 || val > 65535)
        return -1;

    return val;
}

// ------------------------------------------------------------
// Lijep banner pri startu (čisto kozmetika)
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
// Poseban proces koji sluša stdin u server terminalu:
// kad korisnik upiše "exit", traži globalni shutdown servera
// ------------------------------------------------------------
static void runConsoleWatcher(pid_t parentPid)
{
    char line[256];

    printf("[CONSOLE] Type 'exit' or CTRL+C to stop the server.\n");
    fflush(stdout);

    while (fgets(line, sizeof(line), stdin) != NULL) {
        // ukloni newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        if (strcmp(line, "exit") == 0) {
            printf("[CONSOLE] Shutdown requested. Stopping server...\n");
            fflush(stdout);
            kill(parentPid, SIGTERM);   // pošalji signal parent procesu
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
    // ---------------------------
    // Podrazumijevane vrijednosti
    // ---------------------------
    const char *rootDir = "root_directory";  // default ime root foldera
    const char *ip      = "127.0.0.1";
    int         port    = 8080;

    // Ako je korisnik dao argumente, pregazi default
    if (argc >= 2) {
        rootDir = argv[1];
    }

    if (argc >= 3) {
        ip = argv[2];
    }

    if (argc >= 4) {
        int p = validatePort(argv[3]);
        if (p < 0) {
            fprintf(stderr, "ERROR: Invalid port '%s'. Must be 1–65535.\n", argv[3]);
            fprintf(stderr, "Usage: %s [root_directory] [IP] [port]\n", argv[0]);
            return 1;
        }
        port = p;
    }

    // Sigurnosna provjera da ne startaš server na /, /etc, ...
    if (isDangerousRoot(rootDir)) {
        fprintf(stderr,
                "ERROR: Root directory '%s' is a critical system path.\n"
                "Refusing to start for safety reasons.\n",
                rootDir);
        return 1;
    }

    // ---------------------------
    // Kontrola privilegija (sudo / bez sudo)
    // ---------------------------
    uid_t ruid = getuid();   // "stvarni" user (npr. aleksa03)
    uid_t euid = geteuid();  // efektivni (0 ako je sudo ./server)

    int sudoMode = 0;

    if (euid == 0 && ruid != 0) {
        // Pokrenut kao: sudo ./server ...
        sudoMode = 1;
        printf("[SECURITY] Server started with sudo (ruid=%d, euid=0).\n", (int)ruid);
        printf("[SECURITY] Dropping privileges to uid=%d for normal operations.\n", (int)ruid);

        if (seteuid(ruid) != 0) {
            perror("seteuid drop");
            fprintf(stderr, "FATAL: Could not drop root privileges.\n");
            return 1;
        }
        // saved set-uid ostaje 0 → kasnije se može privremeno vratiti na root
    } else if (euid == 0 && ruid == 0) {
        // Pravi root login (ne sudo). Ne preporučeno, ali ne diramo.
        sudoMode = 1;
    } else {
        // Nema sudo, sve radi kao običan user – create_user/delete_user NEĆE moći da rade na sistemskim userima
        sudoMode = 0;
        printf("[SECURITY] Server running as normal user (uid=%d).\n", (int)ruid);
        printf("           System-level adduser/userdel will be disabled.\n");
    }

    // Globalna varijabla koju koriste drugi moduli
    gRootDir = rootDir;

    // Osiguraj da rootDir postoji (radi se već sa običnim uid-om, ne kao root)
    if (ensureRootDirectory(rootDir) < 0) {
        return 1;
    }

    // ---------------------------
    // Signal handler-i
    // ---------------------------
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handleChildSignal;
    sa.sa_flags   = SA_RESTART;   // da accept() ne puca na SIGCHLD
    sigaction(SIGCHLD, &sa, NULL);

    struct sigaction saTerm;
    memset(&saTerm, 0, sizeof(saTerm));
    saTerm.sa_handler = handleShutdownSignal;
    sigaction(SIGTERM, &saTerm, NULL);

    struct sigaction saInt;
    memset(&saInt, 0, sizeof(saInt));
    saInt.sa_handler = handleShutdownSignal; // Ctrl+C radi isto kao SIGTERM
    sigaction(SIGINT, &saInt, NULL);

    // ---------------------------
    // Kreiraj server socket
    // ---------------------------
    int serverFd = createServerSocket(ip, port);
    if (serverFd < 0) {
        fprintf(stderr, "FATAL: Could not bind to %s:%d\n", ip, port);
        return 1;
    }

    printBanner(rootDir, ip, port, sudoMode);

    // ---------------------------
    // Forkaj "console watcher" proces
    // ---------------------------
    pid_t consolePid = fork();
    if (consolePid < 0) {
        perror("fork console");
        close(serverFd);
        return 1;
    }
    if (consolePid == 0) {
        // Dijete: sluša stdin i čeka "exit"
        runConsoleWatcher(getppid());
        // ne vraća se
    }

    // ---------------------------
    // Glavna petlja: prihvatanje klijenata
    // ---------------------------
    while (!shutdownRequested) {
        int clientFd = acceptClient(serverFd);

        if (shutdownRequested) {
            if (clientFd >= 0) close(clientFd);
            break;
        }

        if (clientFd < 0) {
            if (errno == EINTR) {
                // prekinuto signalom → provjeri shutdownRequested u next iteraciji
                continue;
            }
            perror("acceptClient");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(clientFd);
            continue;
        }

        if (pid == 0) {
            // ------------------ CHILD PROCESS ------------------
            close(serverFd);

            Session session;
            initSession(&session);

            ProtocolMessage msg;

            while (1) {
                if (receiveMessage(clientFd, &msg) < 0) {
                    printf("[INFO] Client disconnected.\n");
                    fflush(stdout);
                    break;
                }

                if (msg.command == CMD_EXIT) {
                    // EXIT od klijenta → gasi se SAMO ova sesija
                    ProtocolResponse ok = { STATUS_OK, 0 };
                    sendResponse(clientFd, &ok);
                    break;
                }

                processCommand(clientFd, &msg, &session);
            }

            close(clientFd);
            _exit(0);
        }

        // ------------------ PARENT PROCESS --------------------
        if (childCount < MAX_CHILDREN) {
            children[childCount++] = pid;
        }
        close(clientFd);
    }

    // ---------------------------
    // SERVER SHUTDOWN SEQUENCE
    // ---------------------------
    printf("\n[SHUTDOWN] Server shutting down...\n");

    // Zatvori server socket da niko novi ne može da se spoji
    close(serverFd);

    // Ubij sve child procese koji opslužuju klijente
    for (int i = 0; i < childCount; i++) {
        if (children[i] > 0) {
            kill(children[i], SIGKILL);
        }
    }

    // Ubij i console watcher (ako još živi)
    kill(consolePid, SIGKILL);

    // Pokupi sve preostale procese (zombiji)
    while (waitpid(-1, NULL, 0) > 0) {
        // nothing
    }

    printf("[SHUTDOWN] All client handlers terminated.\n");
    printf("[SHUTDOWN] Server terminated cleanly.\n");

    return 0;
}
