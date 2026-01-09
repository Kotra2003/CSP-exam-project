#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>

#include "../../include/clientCommands.h"
#include "../../include/protocol.h"
#include "../../include/network.h"
#include "../../include/utils.h"

// ============================================================
// ANSI colors for client output
// ============================================================
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"

// Client message helpers
#define ERROR(fmt, ...)   printf(RED "[X] " fmt RESET "\n", ##__VA_ARGS__)
#define SUCCESS(fmt, ...) printf(GREEN "[OK] " fmt RESET "\n", ##__VA_ARGS__)
#define SYNTAX(fmt, ...)  printf(YELLOW "[!] " fmt RESET "\n", ##__VA_ARGS__)

// Upload / download helpers (implemented elsewhere)
extern int uploadFile(int sock, const char *localPath, const char *remotePath);
extern int downloadFile(int sock, const char *remotePath, const char *localPath);

// ============================================================
// Client state
// ============================================================
static const char *g_ip = NULL;
static int g_port = 0;
static char g_username[64] = "";
static char g_currentPath[PATH_SIZE] = "/";

// Background process tracking
static pid_t bgPids[128];
static int bgCount = 0;

// ============================================================
// Public helpers
// ============================================================
void setGlobalServerInfo(const char *ip, int port)
{
    g_ip = ip;
    g_port = port;
}

const char* getCurrentPath()
{
    return g_currentPath;
}

const char* getUsername()
{
    return g_username;
}

void updateCurrentPath(const char *newPath)
{
    if (newPath && newPath[0] != '\0') {
        strncpy(g_currentPath, newPath, PATH_SIZE);
        g_currentPath[PATH_SIZE - 1] = '\0';
    }
}

void registerBackgroundProcess(pid_t pid)
{
    if (bgCount < 128) {
        bgPids[bgCount++] = pid;
    }
}

void unregisterBackgroundProcess(pid_t pid)
{
    for (int i = 0; i < bgCount; i++) {
        if (bgPids[i] == pid) {
            bgPids[i] = bgPids[--bgCount];
            return;
        }
    }
}

int hasActiveBackgroundProcesses(void)
{
    return bgCount > 0;
}


// ============================================================
// Simple tokenizer: splits input by spaces
// ============================================================
int tokenize(char *input, char *tokens[], int maxTokens)
{
    int count = 0;
    char *tok = strtok(input, " ");
    while (tok && count < maxTokens) {
        tokens[count++] = tok;
        tok = strtok(NULL, " ");
    }
    return count;
}

// ============================================================
// Prints client-side error messages for commands
// Only affects UI, not server logic
// ============================================================
static void explainCommandError(const char *cmd,
                                const char *a1,
                                const char *a2,
                                const char *a3)
{
    // Unused arguments (kept for consistency)
    (void)a1;
    (void)a2;
    (void)a3;

    if (strcmp(cmd, "login") == 0) {
        ERROR("Login failed.");
        ERROR(" - You must NOT be logged in");
        ERROR(" - User must exist");
        SYNTAX("login <username>");
        return;
    }

    if (strcmp(cmd, "create_user") == 0) {
        ERROR("User creation failed.");
        ERROR(" - User already exists");
        ERROR(" - Invalid permissions");
        ERROR(" - You must NOT be logged in");
        SYNTAX("create_user <username> <permissions>");
        return;
    }

    if (strcmp(cmd, "delete_user") == 0) {
        ERROR("User deletion failed.");
        ERROR(" - User does not exist");
        ERROR(" - You must NOT be logged in");
        SYNTAX("delete_user <username>");
        return;
    }

    if (strcmp(cmd, "cd") == 0) {
        ERROR("Cannot change directory.");
        ERROR(" - Invalid permissions");
        ERROR(" - Invalid path");
        SYNTAX("cd <directory>");
        return;
    }

    if (strcmp(cmd, "list") == 0) {
        ERROR("List failed.");
        ERROR(" - Invalid path");
        SYNTAX("list [path]");
        return;
    }

    if (strcmp(cmd, "create") == 0) {
        ERROR("Create failed.");
        ERROR(" - Invalid permissions");
        ERROR(" - Invalid path");
        ERROR(" - File/folder already exists");
        SYNTAX("create <path> <permissions> [-d]");
        return;
    }

    if (strcmp(cmd, "chmod") == 0) {
        ERROR("Chmod failed.");
        ERROR(" - Invalid permissions");
        ERROR(" - Invalid path");
        SYNTAX("chmod <path> <permissions>");
        return;
    }

    if (strcmp(cmd, "move") == 0) {
        ERROR("Move failed.");
        ERROR(" - Invalid path");
        SYNTAX("move <source> <destination>");
        return;
    }

    if (strcmp(cmd, "delete") == 0) {
        ERROR("Delete failed.");
        ERROR(" - File/folder does not exist");
        SYNTAX("delete <path>");
        return;
    }

    if (strcmp(cmd, "read") == 0) {
        ERROR("Read failed.");
        ERROR(" - Invalid path");
        SYNTAX("read <path>");
        SYNTAX("read -offset=N <path>");
        return;
    }

    if (strcmp(cmd, "write") == 0) {
        ERROR("Write failed.");
        ERROR(" - Invalid path");
        SYNTAX("write <path>");
        SYNTAX("write -offset=N <path>");
        return;
    }

    if (strcmp(cmd, "upload") == 0) {
        ERROR("Upload failed.");
        ERROR(" - Invalid paths");
        SYNTAX("upload <local> <remote>");
        SYNTAX("upload -b <local> <remote>");
        return;
    }

    if (strcmp(cmd, "download") == 0) {
        ERROR("Download failed.");
        ERROR(" - Invalid paths");
        SYNTAX("download <remote> <local>");
        SYNTAX("download -b <remote> <local>");
        return;
    }

    ERROR("Unknown command error.");
}


// ============================================================
// Send a command without extra data
// Used for simple commands (login, create, delete, chmod, ...)
 // ============================================================
static int sendSimpleCommand(int sock, int cmd,
                             const char *arg1,
                             const char *arg2,
                             const char *arg3)
{
    ProtocolMessage msg;

    // Initialize message
    memset(&msg, 0, sizeof(msg));
    msg.command = cmd;

    // Copy arguments if present
    if (arg1) strncpy(msg.arg1, arg1, ARG_SIZE);
    if (arg2) strncpy(msg.arg2, arg2, ARG_SIZE);
    if (arg3) strncpy(msg.arg3, arg3, ARG_SIZE);

    // Send request
    sendMessage(sock, &msg);

    // Receive response
    ProtocolResponse res;
    if (receiveResponse(sock, &res) < 0) {
        ERROR("No response from server");
        return -1;
    }

    // Return data size on success
    return (res.status == STATUS_OK ? res.dataSize : -1);
}

// ============================================================
// Login helper for background processes
// ============================================================
static int backgroundLogin(int bgSock)
{
    // Background jobs require an existing login
    if (strlen(g_username) == 0)
        return -1;

    ProtocolMessage loginMsg;
    memset(&loginMsg, 0, sizeof(loginMsg));
    loginMsg.command = CMD_LOGIN;
    strncpy(loginMsg.arg1, g_username, ARG_SIZE);

    // Send login request
    sendMessage(bgSock, &loginMsg);

    // Wait for reply
    ProtocolResponse lr;
    if (receiveResponse(bgSock, &lr) < 0 || lr.status != STATUS_OK) {
        return -1;
    }

    return 0;
}

// ============================================================
// Start upload in background process
// ============================================================
static void startBackgroundUpload(const char *local, const char *remote)
{
    pid_t pid = fork();
    if (pid < 0) {
        ERROR("Cannot fork background upload");
        return;
    }

    // Parent: register job and return
    if (pid > 0) {
        registerBackgroundProcess(pid);
        printf(YELLOW "[BG] Upload started (PID=%d): %s -> %s\n" RESET, pid, local, remote);
        return;
    }

    // Child process

    // Detach from terminal input
    close(STDIN_FILENO);

    // Ignore signals in background
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    // Small delay for testing exit behavior
    sleep(5);

    // New server connection
    int bgSock = connectToServer(g_ip, g_port);
    if (bgSock < 0) {
        _exit(1);
    }

    // Authenticate
    if (backgroundLogin(bgSock) < 0) {
        close(bgSock);
        _exit(1);
    }

    // Run upload
    int result = uploadFile(bgSock, local, remote);
    close(bgSock);

    // Print result
    if (result == 0) {
        printf(YELLOW "[Background] Command: upload %s %s concluded\n" RESET, remote, local);
    } else {
        printf(YELLOW "[Background] Command: upload %s %s FAILED\n" RESET, remote, local);
    }

    fflush(stdout);
    _exit(0);
}

// ============================================================
// Start download in background process
// ============================================================
static void startBackgroundDownload(const char *remote, const char *local)
{
    pid_t pid = fork();
    if (pid < 0) {
        ERROR("Cannot fork background download");
        return;
    }

    // Parent: register job and return
    if (pid > 0) {
        registerBackgroundProcess(pid);
        printf(YELLOW "[BG] Download started (PID=%d): %s -> %s\n" RESET, pid, remote, local);
        return;
    }

    // Child process

    // Detach from terminal input
    close(STDIN_FILENO);

    // Ignore signals in background
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    // Small delay for testing exit behavior
    sleep(5);

    // New server connection
    int bgSock = connectToServer(g_ip, g_port);
    if (bgSock < 0) {
        _exit(1);
    }

    // Authenticate
    if (backgroundLogin(bgSock) < 0) {
        close(bgSock);
        _exit(1);
    }

    // Run download
    int result = downloadFile(bgSock, remote, local);
    close(bgSock);

    // Print result
    if (result == 0) {
        printf(YELLOW "[Background] Command: download %s %s concluded\n" RESET, remote, local);
    } else {
        printf(YELLOW "[Background] Command: download %s %s FAILED\n" RESET, remote, local);
    }

    fflush(stdout);
    _exit(0);
}

// ============================================================
// Main client command handler
// ============================================================
int clientHandleInput(int sock, char *input)
{
    char *tokens[10];
    int n = tokenize(input, tokens, 10);
    if (n == 0) return 0;

    char *cmd = tokens[0];

    // ----------------------------
    // LOGIN
    // ----------------------------
    if (strcmp(cmd, "login") == 0) {
        if (n < 2) {
            SYNTAX("Syntax: login <username>");
            return 0;
        }

        if (sendSimpleCommand(sock, CMD_LOGIN, tokens[1], NULL, NULL) >= 0) {
            SUCCESS("Logged in as %s", tokens[1]);
            strncpy(g_username, tokens[1], sizeof(g_username));
            strcpy(g_currentPath, "/");
        } else {
            explainCommandError("login", tokens[1], NULL, NULL);
        }
        return 0;
    }

    // ----------------------------
    // CREATE USER
    // ----------------------------
    if (strcmp(cmd, "create_user") == 0) {
        if (n < 3) {
            SYNTAX("Syntax: create_user <username> <permissions>");
            return 0;
        }

        if (sendSimpleCommand(sock, CMD_CREATE_USER, tokens[1], tokens[2], NULL) >= 0)
            SUCCESS("User %s created", tokens[1]);
        else
            explainCommandError("create_user", tokens[1], tokens[2], NULL);

        return 0;
    }

    // ----------------------------
    // DELETE USER
    // ----------------------------
    if (strcmp(cmd, "delete_user") == 0) {
        if (n < 2) {
            SYNTAX("Syntax: delete_user <username>");
            return 0;
        }

        if (sendSimpleCommand(sock, CMD_DELETE_USER, tokens[1], NULL, NULL) >= 0)
            SUCCESS("User %s deleted", tokens[1]);
        else
            explainCommandError("delete_user", tokens[1], NULL, NULL);

        return 0;
    }

    // ----------------------------
    // CD
    // ----------------------------
    if (strcmp(cmd, "cd") == 0) {
        if (n < 2) {
            SYNTAX("Syntax: cd <directory>");
            return 0;
        }

        ProtocolMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.command = CMD_CD;
        strncpy(msg.arg1, tokens[1], ARG_SIZE);

        sendMessage(sock, &msg);

        ProtocolResponse res;
        if (receiveResponse(sock, &res) < 0) {
            SYNTAX("No response from server");
            return 0;
        }

        if (res.status != STATUS_OK) {
            explainCommandError("cd", tokens[1], NULL, NULL);
            return 0;
        }

        // Update path returned by server
        if (res.dataSize > 0) {
            char newPath[PATH_SIZE];
            if (res.dataSize < PATH_SIZE) {
                recvAll(sock, newPath, res.dataSize);
                newPath[res.dataSize] = '\0';
                updateCurrentPath(newPath);
            }
        }

        return 0;
    }

    // ----------------------------
    // LIST
    // ----------------------------
    if (strcmp(cmd, "list") == 0) {
        const char *path = (n >= 2 ? tokens[1] : "");

        int dataSize = sendSimpleCommand(sock, CMD_LIST, path, NULL, NULL);
        if (dataSize < 0) {
            explainCommandError("list", path, NULL, NULL);
            return 0;
        }

        char *buffer = malloc(dataSize + 1);
        recvAll(sock, buffer, dataSize);
        buffer[dataSize] = '\0';

        printf("%s", buffer);
        free(buffer);

        return 0;
    }

    // -----------------------------------------------------------
    // CREATE command (file or directory)
    // -----------------------------------------------------------
    if (strcmp(cmd, "create") == 0) {
        // Validate argument count
        if (n < 3 || n > 4) {
            SYNTAX("Syntax: create <path> <permissions> [-d]");
            return 0;
        }

        const char *path = tokens[1];
        const char *perm = tokens[2];
        const char *flag = (n == 4 ? tokens[3] : "");

        // Validate optional directory flag
        if (n == 4 && strcmp(flag, "-d") != 0) {
            SYNTAX("Syntax: create <path> <permissions> [-d]");
            return 0;
        }

        // Send CREATE request
        if (sendSimpleCommand(sock, CMD_CREATE, path, perm, flag) < 0)
            explainCommandError("create", path, perm, flag);
        else
            SUCCESS("Created");

        return 0;
    }

    // -----------------------------------------------------------
    // CHMOD command
    // -----------------------------------------------------------
    if (strcmp(cmd, "chmod") == 0) {
        // Validate arguments
        if (n < 3) {
            SYNTAX("Syntax: chmod <path> <permissions>");
            return 0;
        }

        // Send CHMOD request
        if (sendSimpleCommand(sock, CMD_CHMOD, tokens[1], tokens[2], NULL) < 0)
            explainCommandError("chmod", tokens[1], tokens[2], NULL);
        else
            SUCCESS("Permissions changed");

        return 0;
    }

    // -----------------------------------------------------------
    // MOVE command
    // -----------------------------------------------------------
    if (strcmp(cmd, "move") == 0) {
        // Validate arguments
        if (n < 3) {
            SYNTAX("Syntax: move <src> <dst>");
            return 0;
        }

        // Send MOVE request
        if (sendSimpleCommand(sock, CMD_MOVE, tokens[1], tokens[2], NULL) < 0)
            explainCommandError("move", tokens[1], tokens[2], NULL);
        else
            SUCCESS("Moved");

        return 0;
    }

    // -----------------------------------------------------------
    // DELETE command
    // -----------------------------------------------------------
    if (strcmp(cmd, "delete") == 0) {
        // Validate arguments
        if (n < 2) {
            SYNTAX("Syntax: delete <path>");
            return 0;
        }

        // Send DELETE request
        if (sendSimpleCommand(sock, CMD_DELETE, tokens[1], NULL, NULL) < 0)
            explainCommandError("delete", tokens[1], NULL, NULL);
        else
            SUCCESS("Deleted");

        return 0;
    }

    // -----------------------------------------------------------
    // READ command
    // Supports optional offset parameter
    // -----------------------------------------------------------
    if (strcmp(cmd, "read") == 0) {
        ProtocolMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.command = CMD_READ;

        // Parse arguments:
        // read <path>
        if (n == 2) {
            strncpy(msg.arg1, tokens[1], ARG_SIZE);
        }
        // read -offset=N <path>
        else if (n == 3 && strncmp(tokens[1], "-offset=", 8) == 0) {
            strncpy(msg.arg1, tokens[2], ARG_SIZE);
            strncpy(msg.arg2, tokens[1] + 8, ARG_SIZE);
        }
        else {
            SYNTAX("Syntax: read <path> OR read -offset=N <path>");
            return 0;
        }

        // Send READ request
        sendMessage(sock, &msg);

        // Receive server response
        ProtocolResponse res;
        if (receiveResponse(sock, &res) < 0 || res.status != STATUS_OK) {
            explainCommandError("read", msg.arg1, msg.arg2, NULL);
            return 0;
        }

        // Receive file content
        int size = res.dataSize;
        char *buffer = malloc(size + 1);
        recvAll(sock, buffer, size);
        buffer[size] = '\0';

        printf("%s\n", buffer);
        free(buffer);

        return 0;
    }

    // -----------------------------------------------------------
    // WRITE command
    // Reads input from stdin and sends it to the server
    // -----------------------------------------------------------
    if (strcmp(cmd, "write") == 0) {
        ProtocolMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.command = CMD_WRITE;

        // Parse arguments:
        // write <path>
        if (n == 2) {
            strncpy(msg.arg1, tokens[1], ARG_SIZE);
        }
        // write -offset=N <path>
        else if (n == 3 && strncmp(tokens[1], "-offset=", 8) == 0) {
            strncpy(msg.arg1, tokens[2], ARG_SIZE);
            strncpy(msg.arg2, tokens[1] + 8, ARG_SIZE);
        }
        else {
            SYNTAX("Syntax: write <path> OR write -offset=N <path>");
            return 0;
        }

        // Send WRITE request header
        sendMessage(sock, &msg);

        // Wait for server acknowledgment
        ProtocolResponse ack;
        if (receiveResponse(sock, &ack) < 0 || ack.status != STATUS_OK) {
            explainCommandError("write", msg.arg1, msg.arg2, NULL);
            return 0;
        }

        // Read data from stdin
        printf("Enter text (press ENTER then Ctrl+D):\n");

        char tmp[4096];
        char *buffer = NULL;
        int total = 0, cap = 0, r;

        // Dynamically read input until EOF
        while ((r = read(STDIN_FILENO, tmp, sizeof(tmp))) > 0) {
            if (total + r > cap) {
                int newCap = (cap == 0 ? 4096 : cap * 2);
                while (newCap < total + r) newCap *= 2;
                buffer = realloc(buffer, newCap);
                cap = newCap;
            }

            memcpy(buffer + total, tmp, r);
            total += r;
        }

        // Remove trailing newline if present
        if (total > 0 && buffer[total - 1] == '\n') {
            total--;
        }

        // Send data size and payload
        int size = total;
        sendAll(sock, &size, sizeof(int));
        if (size > 0)
            sendAll(sock, buffer, size);

        free(buffer);

        // Receive final server response
        ProtocolResponse fin;
        receiveResponse(sock, &fin);

        if (fin.status == STATUS_OK)
            SUCCESS("Wrote %d bytes", fin.dataSize);
        else
            explainCommandError("write", msg.arg1, msg.arg2, NULL);

        return 0;
    }

    // -----------------------------------------------------------
    // UPLOAD command (foreground and background)
    // -----------------------------------------------------------
    if (strcmp(cmd, "upload") == 0) {
        // Background upload
        if (n == 4 && strcmp(tokens[1], "-b") == 0) {
            startBackgroundUpload(tokens[2], tokens[3]);
            return 0;
        }

        // Foreground upload
        if (n == 3) {
            if (uploadFile(sock, tokens[1], tokens[2]) < 0) {
                explainCommandError("upload", tokens[1], tokens[2], NULL);
            } else {
                SUCCESS("Upload completed: %s -> %s", tokens[1], tokens[2]);
            }
            return 0;
        }

        SYNTAX("Syntax: upload <local> <remote> OR upload -b <local> <remote>");
        return 0;
    }

    // -----------------------------------------------------------
    // DOWNLOAD command (foreground and background)
    // -----------------------------------------------------------
    if (strcmp(cmd, "download") == 0) {
        // Background download
        if (n == 4 && strcmp(tokens[1], "-b") == 0) {
            startBackgroundDownload(tokens[2], tokens[3]);
            return 0;
        }

        // Foreground download
        if (n == 3) {
            if (downloadFile(sock, tokens[1], tokens[2]) < 0) {
                explainCommandError("download", tokens[1], tokens[2], NULL);
            } else {
                SUCCESS("Download completed: %s -> %s", tokens[1], tokens[2]);
            }
            return 0;
        }

        // Invalid syntax
        SYNTAX("Syntax: download <remote> <local> OR download -b <remote> <local>");
        return 0;
    }

    // -----------------------------------------------------------
    // EXIT command
    // -----------------------------------------------------------
    if (strcmp(cmd, "exit") == 0) {
        // Do not allow exit while background transfers are running
        if (hasActiveBackgroundProcesses()) {
            ERROR("Cannot exit: %d background transfer(s) still running", bgCount);
            printf("Wait for them to finish or use Ctrl+C\n");
            return 0;
        }

        // Notify server and terminate client loop
        sendSimpleCommand(sock, CMD_EXIT, NULL, NULL, NULL);
        return 1;
    }

    // Unknown command fallback
    ERROR("Unknown command: %s", cmd);
    return 0;
}
