#include <stdio.h>      // Standard I/O (printf)
#include <string.h>     // String manipulation (strncpy, strtok)
#include <stdlib.h>     // Standard library functions
#include <sys/socket.h> // Socket-related functions
#include <unistd.h>     // POSIX API (fork, read, close)
#include <signal.h>     // Signal handling

#include "../../include/clientCommands.h" // Client command interface
#include "../../include/protocol.h"       // Protocol definitions
#include "../../include/network.h"        // Networking helpers
#include "../../include/utils.h"          // Utility functions

// ============================================================================
// ANSI color definitions for client UI output
// ============================================================================
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"

// Macros for formatted client messages
#define ERROR(fmt, ...)   printf(RED    "[X] " fmt RESET "\n", ##__VA_ARGS__)
#define SUCCESS(fmt, ...) printf(GREEN  "[OK] "    fmt RESET "\n", ##__VA_ARGS__)
#define SYNTAX(fmt, ...)  printf(YELLOW "[!] " fmt RESET "\n", ##__VA_ARGS__)

// ============================================================================
// External upload/download functions implemented in networkClient.c
// ============================================================================
extern int uploadFile(int sock, const char *localPath, const char *remotePath);
extern int downloadFile(int sock, const char *remotePath, const char *localPath);

// ============================================================================
// Global client state (local to this translation unit)
// ============================================================================
static const char *g_ip = NULL;     // Server IP address
static int g_port = 0;              // Server port
static char g_username[64] = "";    // Logged-in username (client-side only)
static char g_currentPath[PATH_SIZE] = "/"; // Current virtual path for prompt

// Background process tracking (for async upload/download)
static pid_t bgPids[128];           // PIDs of background processes
static int bgCount = 0;             // Number of active background processes

// ============================================================================
// Public helper functions
// ============================================================================

// Store server connection info for later reuse (e.g. background processes)
void setGlobalServerInfo(const char *ip, int port)
{
    g_ip = ip;
    g_port = port;
}

// Return current client-side path (used for prompt rendering)
const char* getCurrentPath()
{
    return g_currentPath;
}

// Return current username (empty string if not logged in)
const char* getUsername()
{
    return g_username;
}

// Update client-side current path after successful cd
void updateCurrentPath(const char *newPath)
{
    if (newPath && newPath[0] != '\0') {
        strncpy(g_currentPath, newPath, PATH_SIZE);
        g_currentPath[PATH_SIZE - 1] = '\0'; // Ensure null-termination
    }
}

// Register a background process PID
void registerBackgroundProcess(pid_t pid)
{
    if (bgCount < 128) {
        bgPids[bgCount++] = pid;
    }
}

// Remove a background process PID when it finishes
void unregisterBackgroundProcess(pid_t pid)
{
    for (int i = 0; i < bgCount; i++) {
        if (bgPids[i] == pid) {
            bgPids[i] = bgPids[--bgCount];
            return;
        }
    }
}

// Check whether any background transfers are still running
int hasActiveBackgroundProcesses(void)
{
    return (bgCount > 0);
}

// ============================================================================
// Simple command tokenizer
// Splits input string by spaces into tokens
// ============================================================================
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

// ============================================================================
// Client-side error explanation helper
// This function does NOT affect the protocol or server logic.
// It only prints user-friendly error messages on the client side.
// ============================================================================
static void explainCommandError(const char *cmd,
                                const char *a1,
                                const char *a2,
                                const char *a3)
{
    // Arguments are intentionally unused here.
    // They are kept for future extensibility and interface consistency.
    (void)a1;
    (void)a2;
    (void)a3;

    // Login command error handling
    if (strcmp(cmd, "login") == 0) {
        ERROR("Login failed: user does not exist or you are already logged in.");
        SYNTAX("Syntax: login <username>");
        return;
    }

    // User creation error handling
    if (strcmp(cmd, "create_user") == 0) {
        ERROR("User creation failed.");
        ERROR("Possible reasons:");
        ERROR(" - user already exists");
        ERROR(" - invalid permissions (must be octal, e.g. 700)");
        SYNTAX("Syntax: create_user <username> <permissions>");
        return;
    }

    // User deletion error handling
    if (strcmp(cmd, "delete_user") == 0) {
        ERROR("User deletion failed.");
        ERROR("You must NOT be logged in and the user must exist.");
        SYNTAX("Syntax: delete_user <username>");
        return;
    }

    // Change directory error handling
    if (strcmp(cmd, "cd") == 0) {
        ERROR("Change directory failed.");
        ERROR("Possible reasons:");
        ERROR(" - directory does not exist");
        ERROR(" - directory is outside your home directory");
        SYNTAX("Syntax: cd <directory>");
        return;
    }

    // List command error handling
    if (strcmp(cmd, "list") == 0) {
        ERROR("List command failed.");
        ERROR("Possible reasons:");
        ERROR(" - directory does not exist");
        ERROR(" - invalid path");
        SYNTAX("Syntax: list [path]");
        return;
    }

    // Create file/directory error handling
    if (strcmp(cmd, "create") == 0) {
        ERROR("Create operation failed.");
        ERROR("Possible reasons:");
        ERROR(" - file or directory already exists");
        ERROR(" - invalid permissions (0–777, octal)");
        ERROR(" - invalid path");
        SYNTAX("Syntax: create <path> <permissions> [-d]");
        return;
    }

    // Chmod error handling
    if (strcmp(cmd, "chmod") == 0) {
        ERROR("Permission change failed.");
        ERROR("Possible reasons:");
        ERROR(" - file does not exist");
        ERROR(" - invalid permission value (octal)");
        SYNTAX("Syntax: chmod <path> <permissions>");
        return;
    }

    // Move/rename error handling
    if (strcmp(cmd, "move") == 0) {
        ERROR("Move operation failed.");
        ERROR("Possible reasons:");
        ERROR(" - source does not exist");
        ERROR(" - destination already exists");
        ERROR(" - invalid path");
        SYNTAX("Syntax: move <source> <destination>");
        return;
    }

    // Delete error handling
    if (strcmp(cmd, "delete") == 0) {
        ERROR("Delete operation failed.");
        ERROR("Possible reasons:");
        ERROR(" - file or directory does not exist");
        SYNTAX("Syntax: delete <path>");
        return;
    }

    // Read error handling
    if (strcmp(cmd, "read") == 0) {
        ERROR("Read operation failed.");
        ERROR("Possible reasons:");
        ERROR(" - file does not exist");
        ERROR(" - invalid offset");
        SYNTAX("Syntax: read <path>");
        SYNTAX("        read -offset=N <path>");
        return;
    }

    // Write error handling
    if (strcmp(cmd, "write") == 0) {
        ERROR("Write operation failed.");
        ERROR("Possible reasons:");
        ERROR(" - invalid path");
        ERROR(" - invalid offset");
        SYNTAX("Syntax: write <path>");
        SYNTAX("        write -offset=N <path>");
        return;
    }

    // Upload error handling
    if (strcmp(cmd, "upload") == 0) {
        ERROR("Upload failed.");
        ERROR("Possible reasons:");
        ERROR(" - local file does not exist");
        ERROR(" - invalid remote path");
        SYNTAX("Syntax: upload <local> <remote>");
        SYNTAX("        upload -b <local> <remote>");
        return;
    }

    // Download error handling
    if (strcmp(cmd, "download") == 0) {
        ERROR("Download failed.");
        ERROR("Possible reasons:");
        ERROR(" - remote file does not exist");
        SYNTAX("Syntax: download <remote> <local>");
        SYNTAX("        download -b <remote> <local>");
        return;
    }

    // Fallback for unknown command errors
    ERROR("Command failed due to an unknown error.");
    ERROR("Tip: check command syntax and arguments.");
}

// ============================================================================
// Send a simple command without payload data
// Used for commands like login, create, delete, chmod, etc.
// ============================================================================
static int sendSimpleCommand(int sock, int cmd,
                             const char *arg1,
                             const char *arg2,
                             const char *arg3)
{
    ProtocolMessage msg;

    // Clear the message structure
    memset(&msg, 0, sizeof(msg));
    msg.command = cmd;

    // Copy arguments if provided
    if (arg1) strncpy(msg.arg1, arg1, ARG_SIZE);
    if (arg2) strncpy(msg.arg2, arg2, ARG_SIZE);
    if (arg3) strncpy(msg.arg3, arg3, ARG_SIZE);

    // Send request to server
    sendMessage(sock, &msg);

    // Receive server response
    ProtocolResponse res;
    if (receiveResponse(sock, &res) < 0) {
        ERROR("No response from server (connection issue?)");
        return -1;
    }

    // On success return dataSize, otherwise signal error
    return (res.status == STATUS_OK ? res.dataSize : -1);
}

// ============================================================================
// Helper: Background login
// Used by background upload/download processes to authenticate
// ============================================================================
static int backgroundLogin(int bgSock)
{
    // Background operations require an already logged-in user
    if (strlen(g_username) == 0)
        return -1;

    // Prepare login message using existing client username
    ProtocolMessage loginMsg;
    memset(&loginMsg, 0, sizeof(loginMsg));
    loginMsg.command = CMD_LOGIN;
    strncpy(loginMsg.arg1, g_username, ARG_SIZE);

    // Send login request to server
    sendMessage(bgSock, &loginMsg);

    // Wait for server response
    ProtocolResponse lr;
    if (receiveResponse(bgSock, &lr) < 0 || lr.status != STATUS_OK) {
        return -1;
    }

    return 0;
}

// ============================================================================
// Background upload handler
// Runs upload operation in a separate process
// ============================================================================
static void startBackgroundUpload(const char *local, const char *remote)
{
    // Fork a new process for background execution
    pid_t pid = fork();
    if (pid < 0) {
        ERROR("Cannot fork background upload");
        return;
    }

    // Parent process: register background job and return immediately
    if (pid > 0) {
        registerBackgroundProcess(pid);
        printf(YELLOW "[BG] Upload started (PID=%d): %s -> %s\n" RESET, pid, local, remote);
        printf(YELLOW "[BG] Running in background (sleep 5s for demo)...\n" RESET);
        return;
    }

    // ------------------------------------------------------------------------
    // Child process: perform the background upload
    // ------------------------------------------------------------------------

    // Detach from terminal input
    close(STDIN_FILENO);

    // Ignore signals that could terminate the background job
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    // Demo delay to clearly show background execution
    printf(YELLOW "[BG PID=%d] Starting upload in 5 seconds...\n" RESET, getpid());
    sleep(5);

    // Create a new connection to the server
    int bgSock = connectToServer(g_ip, g_port);
    if (bgSock < 0) {
        _exit(1);
    }

    // Authenticate using background login helper
    if (backgroundLogin(bgSock) < 0) {
        close(bgSock);
        _exit(1);
    }

    // Perform the upload operation
    printf(YELLOW "[BG PID=%d] Uploading %s -> %s...\n" RESET, getpid(), local, remote);

    int result = uploadFile(bgSock, local, remote);
    close(bgSock);

    // Report result of background upload
    if (result == 0) {
        printf(YELLOW "[Background] Command: upload %s %s concluded\n" RESET, remote, local);
    } else {
        printf(YELLOW "[Background] Command: upload %s %s FAILED\n" RESET, remote, local);
    }

    // Ensure output is flushed before exiting child process
    fflush(stdout);
    _exit(0);
}
// ============================================================================
// Background download handler
// Runs download operation in a separate process
// ============================================================================
static void startBackgroundDownload(const char *remote, const char *local)
{
    // Fork a new process for background execution
    pid_t pid = fork();
    if (pid < 0) {
        ERROR("Cannot fork background download");
        return;
    }

    // Parent process: register background job and return immediately
    if (pid > 0) {
        registerBackgroundProcess(pid);
        printf(YELLOW "[BG] Download started (PID=%d): %s -> %s\n" RESET, pid, remote, local);
        printf(YELLOW "[BG] Running in background (sleep 5s for demo)...\n" RESET);
        return;
    }

    // ------------------------------------------------------------------------
    // Child process: perform the background download
    // ------------------------------------------------------------------------

    // Detach from terminal input
    close(STDIN_FILENO);

    // Ignore signals that could interrupt the background job
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    // Demo delay to make background execution visible
    printf(YELLOW "[BG PID=%d] Starting download in 5 seconds...\n" RESET, getpid());
    sleep(5);

    // Create a new connection to the server
    int bgSock = connectToServer(g_ip, g_port);
    if (bgSock < 0) {
        _exit(1);
    }

    // Authenticate using background login helper
    if (backgroundLogin(bgSock) < 0) {
        close(bgSock);
        _exit(1);
    }

    // Perform the download operation
    printf(YELLOW "[BG PID=%d] Downloading %s -> %s...\n" RESET, getpid(), remote, local);

    int result = downloadFile(bgSock, remote, local);
    close(bgSock);

    // Report result of background download
    if (result == 0) {
        printf(YELLOW "[Background] Command: download %s %s concluded\n" RESET, remote, local);
    } else {
        printf(YELLOW "[Background] Command: download %s %s FAILED\n" RESET, remote, local);
    }

    // Ensure output is flushed before exiting child process
    fflush(stdout);
    _exit(0);
}

// ============================================================================
// Main client command handler
// Parses input, sends requests to the server and handles responses
// ============================================================================
int clientHandleInput(int sock, char *input)
{
    char *tokens[10];
    int n = tokenize(input, tokens, 10);
    if (n == 0) return 0;

    char *cmd = tokens[0];

    // -----------------------------------------------------------
    // LOGIN command
    // -----------------------------------------------------------
    if (strcmp(cmd, "login") == 0) {
        if (n < 2) {
            SYNTAX("Syntax: login <username>");
            return 0;
        }

        // Send login request to the server
        if (sendSimpleCommand(sock, CMD_LOGIN, tokens[1], NULL, NULL) >= 0) {
            SUCCESS("Logged in as %s", tokens[1]);
            strncpy(g_username, tokens[1], sizeof(g_username));
            strcpy(g_currentPath, "/");
        }
        else {
            explainCommandError("login", tokens[1], NULL, NULL);
        }
        return 0;
    }

    // -----------------------------------------------------------
    // CREATE USER command
    // -----------------------------------------------------------
    if (strcmp(cmd, "create_user") == 0) {
        if (n < 3) {
            SYNTAX("Syntax: create_user <username> <permissions>");
            return 0;
        }

        // Send user creation request
        if (sendSimpleCommand(sock, CMD_CREATE_USER, tokens[1], tokens[2], NULL) >= 0)
            SUCCESS("User %s created", tokens[1]);
        else
            explainCommandError("create_user", tokens[1], tokens[2], NULL);

        return 0;
    }

    // -----------------------------------------------------------
    // DELETE USER command
    // -----------------------------------------------------------
    if (strcmp(cmd, "delete_user") == 0) {
        if (n < 2) {
            SYNTAX("Syntax: delete_user <username>");
            return 0;
        }

        // Send user deletion request
        if (sendSimpleCommand(sock, CMD_DELETE_USER, tokens[1], NULL, NULL) >= 0)
            SUCCESS("User %s deleted", tokens[1]);
        else
            explainCommandError("delete_user", tokens[1], NULL, NULL);

        return 0;
    }

    // -----------------------------------------------------------
    // CD command
    // -----------------------------------------------------------
    if (strcmp(cmd, "cd") == 0) {
        // Check syntax
        if (n < 2) {
            SYNTAX("Syntax: cd <directory>");
            return 0;
        }

        // Build and send CD request
        ProtocolMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.command = CMD_CD;
        strncpy(msg.arg1, tokens[1], ARG_SIZE);

        sendMessage(sock, &msg);

        // Receive server response
        ProtocolResponse res;
        if (receiveResponse(sock, &res) < 0) {
            SYNTAX("No response from server");
            return 0;
        }

        // Handle error response
        if (res.status != STATUS_OK) {
            explainCommandError("cd", tokens[1], NULL, NULL);
            return 0;
        }

        // Server sends new current path on success
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

    // -----------------------------------------------------------
    // LIST command
    // -----------------------------------------------------------
    if (strcmp(cmd, "list") == 0) {
        // Optional path argument
        const char *path = (n >= 2 ? tokens[1] : "");

        // Send LIST request and get expected data size
        int dataSize = sendSimpleCommand(sock, CMD_LIST, path, NULL, NULL);

        if (dataSize < 0) {
            explainCommandError("list", path, NULL, NULL);
            return 0;
        }

        // Receive directory listing from server
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
