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

// ============================================================================
// Boje za UI
// ============================================================================
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"

#define ERROR(fmt, ...)   printf(RED "✗ " fmt RESET "\n", ##__VA_ARGS__)
#define SUCCESS(fmt, ...) printf(GREEN "✓ " fmt RESET "\n", ##__VA_ARGS__)

// ============================================================================
// External upload/download from networkClient.c
// ============================================================================
extern int uploadFile(int sock, const char *localPath, const char *remotePath);
extern int downloadFile(int sock, const char *remotePath, const char *localPath);

// ============================================================================
// Globals
// ============================================================================
static const char *g_ip = NULL;
static int g_port = 0;
static char g_username[64] = "";
static char g_currentPath[PATH_SIZE] = "/";

static pid_t bgPids[128];
static int bgCount = 0;

// ============================================================================
// Public functions
// ============================================================================
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
    return (bgCount > 0);
}

// ============================================================================
// Tokenizer
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
// Simple command sender
// ============================================================================
static int sendSimpleCommand(int sock, int cmd, const char *arg1, const char *arg2, const char *arg3)
{
    ProtocolMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.command = cmd;

    if (arg1) strncpy(msg.arg1, arg1, ARG_SIZE);
    if (arg2) strncpy(msg.arg2, arg2, ARG_SIZE);
    if (arg3) strncpy(msg.arg3, arg3, ARG_SIZE);

    sendMessage(sock, &msg);

    ProtocolResponse res;
    if (receiveResponse(sock, &res) < 0) {
        ERROR("No response from server");
        return -1;
    }

    return (res.status == STATUS_OK ? res.dataSize : -1);
}

// ============================================================================
// Helper: Background login
// ============================================================================
static int backgroundLogin(int bgSock)
{
    if (strlen(g_username) == 0)
        return -1;

    ProtocolMessage loginMsg;
    memset(&loginMsg, 0, sizeof(loginMsg));
    loginMsg.command = CMD_LOGIN;
    strncpy(loginMsg.arg1, g_username, ARG_SIZE);

    sendMessage(bgSock, &loginMsg);

    ProtocolResponse lr;
    if (receiveResponse(bgSock, &lr) < 0 || lr.status != STATUS_OK) {
        return -1;
    }

    return 0;
}

// BACKGROUND UPLOAD
static void startBackgroundUpload(const char *local, const char *remote)
{
    pid_t pid = fork();
    if (pid < 0) {
        ERROR("Cannot fork background upload");
        return;
    }

    if (pid > 0) {
        registerBackgroundProcess(pid);
        printf(YELLOW "[BG] Upload started (PID=%d): %s -> %s\n" RESET, pid, local, remote);
        printf(YELLOW "[BG] Running in background (sleep 5s for demo)...\n" RESET);
        return;
    }

    // CHILD PROCESS
    close(STDIN_FILENO);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    // DEMO: Sleep da se vidi da je background
    printf(YELLOW "[BG PID=%d] Starting upload in 5 seconds...\n" RESET, getpid());
    sleep(5);

    int bgSock = connectToServer(g_ip, g_port);
    if (bgSock < 0) {
        _exit(1);
    }

    if (backgroundLogin(bgSock) < 0) {
        close(bgSock);
        _exit(1);
    }

    printf(YELLOW "[BG PID=%d] Uploading %s -> %s...\n" RESET, getpid(), local, remote);
    
    int result = uploadFile(bgSock, local, remote);
    close(bgSock);

    if (result == 0) {
        printf(YELLOW "[Background] Command: upload %s %s concluded\n" RESET, remote, local);
    } else {
        printf(YELLOW "[Background] Command: upload %s %s FAILED\n" RESET, remote, local);
    }

    fflush(stdout);
    _exit(0);
}

// BACKGROUND DOWNLOAD
static void startBackgroundDownload(const char *remote, const char *local)
{
    pid_t pid = fork();
    if (pid < 0) {
        ERROR("Cannot fork background download");
        return;
    }

    if (pid > 0) {
        registerBackgroundProcess(pid);
        printf(YELLOW "[BG] Download started (PID=%d): %s -> %s\n" RESET, pid, remote, local);
        printf(YELLOW "[BG] Running in background (sleep 5s for demo)...\n" RESET);
        return;
    }

    // CHILD PROCESS
    close(STDIN_FILENO);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    // DEMO: Sleep da se vidi da je background
    printf(YELLOW "[BG PID=%d] Starting download in 5 seconds...\n" RESET, getpid());
    sleep(5);

    int bgSock = connectToServer(g_ip, g_port);
    if (bgSock < 0) {
        _exit(1);
    }

    if (backgroundLogin(bgSock) < 0) {
        close(bgSock);
        _exit(1);
    }

    printf(YELLOW "[BG PID=%d] Downloading %s -> %s...\n" RESET, getpid(), remote, local);
    
    int result = downloadFile(bgSock, remote, local);
    close(bgSock);

    if (result == 0) {
        printf(YELLOW "[Background] Command: download %s %s concluded\n" RESET, remote, local);
    } else {
        printf(YELLOW "[Background] Command: download %s %s FAILED\n" RESET, remote, local);
    }

    fflush(stdout);
    _exit(0);
}

// ============================================================================
// MAIN COMMAND HANDLER
// ============================================================================
int clientHandleInput(int sock, char *input)
{
    char *tokens[10];
    int n = tokenize(input, tokens, 10);
    if (n == 0) return 0;

    char *cmd = tokens[0];

    // -----------------------------------------------------------
    // LOGIN
    if (strcmp(cmd, "login") == 0) {
        if (n < 2) { 
            ERROR("Usage: login <username>"); 
            return 0; 
        }

        if (sendSimpleCommand(sock, CMD_LOGIN, tokens[1], NULL, NULL) >= 0) {
            SUCCESS("Logged in as %s", tokens[1]);
            strncpy(g_username, tokens[1], sizeof(g_username));
            strcpy(g_currentPath, "/");
        }
        else {
            ERROR("Login failed");
        }
        return 0;
    }

    // -----------------------------------------------------------
    // CREATE USER
    if (strcmp(cmd, "create_user") == 0) {
        if (n < 3) { 
            ERROR("Usage: create_user <username> <permissions>"); 
            return 0; 
        }
        
        if (sendSimpleCommand(sock, CMD_CREATE_USER, tokens[1], tokens[2], NULL) >= 0)
            SUCCESS("User %s created", tokens[1]);
        else
            ERROR("Failed to create user");
        
        return 0;
    }

    // -----------------------------------------------------------
    // DELETE USER
    if (strcmp(cmd, "delete_user") == 0) {
        if (n < 2) { 
            ERROR("Usage: delete_user <username>"); 
            return 0; 
        }
        
        if (sendSimpleCommand(sock, CMD_DELETE_USER, tokens[1], NULL, NULL) >= 0)
            SUCCESS("User %s deleted", tokens[1]);
        else
            ERROR("Failed to delete user");
        
        return 0;
    }

    // -----------------------------------------------------------
    // CD
    if (strcmp(cmd, "cd") == 0) {
        if (n < 2) { 
            ERROR("Usage: cd <directory>"); 
            return 0; 
        }
        
        ProtocolMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.command = CMD_CD;
        strncpy(msg.arg1, tokens[1], ARG_SIZE);
        
        sendMessage(sock, &msg);
        
        ProtocolResponse res;
        if (receiveResponse(sock, &res) < 0) {
            ERROR("No response from server");
            return 0;
        }
        
        if (res.status != STATUS_OK) {
            ERROR("Cannot change to '%s'", tokens[1]);
            return 0;
        }
        
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
    // LIST
    if (strcmp(cmd, "list") == 0) {
        const char *path = (n >= 2 ? tokens[1] : "");
        int dataSize = sendSimpleCommand(sock, CMD_LIST, path, NULL, NULL);

        if (dataSize < 0) {
            ERROR("Failed to list directory");
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
    // CREATE
    if (strcmp(cmd, "create") == 0) {
        if (n < 3 || n > 4) {
            ERROR("Usage: create <path> <permissions> [-d]");
            return 0;
        }

        const char *path = tokens[1];
        const char *perm = tokens[2];
        const char *flag = (n == 4 ? tokens[3] : "");

        if (n == 4 && strcmp(flag, "-d") != 0) {
            ERROR("Usage: create <path> <permissions> [-d]");
            return 0;
        }

        if (sendSimpleCommand(sock, CMD_CREATE, path, perm, flag) < 0)
            ERROR("Create failed");
        else
            SUCCESS("Created");

        return 0;
    }

    // -----------------------------------------------------------
    // CHMOD
    if (strcmp(cmd, "chmod") == 0) {
        if (n < 3) { 
            ERROR("Usage: chmod <path> <permissions>"); 
            return 0; 
        }
        
        if (sendSimpleCommand(sock, CMD_CHMOD, tokens[1], tokens[2], NULL) < 0)
            ERROR("Failed to change permissions");
        else
            SUCCESS("Permissions changed");
        
        return 0;
    }

    // -----------------------------------------------------------
    // MOVE
    if (strcmp(cmd, "move") == 0) {
        if (n < 3) { 
            ERROR("Usage: move <src> <dst>"); 
            return 0; 
        }
        
        if (sendSimpleCommand(sock, CMD_MOVE, tokens[1], tokens[2], NULL) < 0)
            ERROR("Failed to move");
        else
            SUCCESS("Moved");
        
        return 0;
    }

    // -----------------------------------------------------------
    // DELETE
    if (strcmp(cmd, "delete") == 0) {
        if (n < 2) { 
            ERROR("Usage: delete <path>"); 
            return 0; 
        }
        
        if (sendSimpleCommand(sock, CMD_DELETE, tokens[1], NULL, NULL) < 0)
            ERROR("Failed to delete");
        else
            SUCCESS("Deleted");
        
        return 0;
    }

    // -----------------------------------------------------------
    // READ
    if (strcmp(cmd, "read") == 0) {
        ProtocolMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.command = CMD_READ;

        if (n == 2) {
            strncpy(msg.arg1, tokens[1], ARG_SIZE);
        }
        else if (n == 3 && strncmp(tokens[1], "-offset=", 8) == 0) {
            strncpy(msg.arg1, tokens[2], ARG_SIZE);
            strncpy(msg.arg2, tokens[1] + 8, ARG_SIZE);
        }
        else {
            ERROR("Usage: read <path> OR read -offset=N <path>");
            return 0;
        }

        sendMessage(sock, &msg);

        ProtocolResponse res;
        if (receiveResponse(sock, &res) < 0 || res.status != STATUS_OK) {
            ERROR("Failed to read file");
            return 0;
        }

        int size = res.dataSize;
        char *buffer = malloc(size + 1);
        recvAll(sock, buffer, size);
        buffer[size] = '\0';

        printf("%s\n", buffer);
        free(buffer);
        
        return 0;
    }

    // -----------------------------------------------------------
    // WRITE
    if (strcmp(cmd, "write") == 0) {
        ProtocolMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.command = CMD_WRITE;

        if (n == 2) {
            strncpy(msg.arg1, tokens[1], ARG_SIZE);
        }
        else if (n == 3 && strncmp(tokens[1], "-offset=", 8) == 0) {
            strncpy(msg.arg1, tokens[2], ARG_SIZE);
            strncpy(msg.arg2, tokens[1] + 8, ARG_SIZE);
        }
        else {
            ERROR("Usage: write <path> OR write -offset=N <path>");
            return 0;
        }

        sendMessage(sock, &msg);

        ProtocolResponse ack;
        if (receiveResponse(sock, &ack) < 0 || ack.status != STATUS_OK) {
            ERROR("Write rejected");
            return 0;
        }

        printf("Enter text (press ENTER then Ctrl+D):\n");
        
        char tmp[4096];
        char *buffer = NULL;
        int total = 0, cap = 0, r;

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

        // UKLONI NEWLINE SA KRAJA AKO POSTOJI
        if (total > 0 && buffer[total - 1] == '\n') {
            total--;  // Ukloni newline
        }

        int size = total;
        sendAll(sock, &size, sizeof(int));
        if (size > 0)
            sendAll(sock, buffer, size);

        free(buffer);

        ProtocolResponse fin;
        receiveResponse(sock, &fin);

        if (fin.status == STATUS_OK)
            SUCCESS("Wrote %d bytes", fin.dataSize);
        else
            ERROR("Write failed");

        return 0;
    }

    // -----------------------------------------------------------
    // UPLOAD (foreground)
    if (strcmp(cmd, "upload") == 0) {
        if (n == 4 && strcmp(tokens[1], "-b") == 0) {
            startBackgroundUpload(tokens[2], tokens[3]);
            return 0;
        }

        if (n == 3) {
            if (uploadFile(sock, tokens[1], tokens[2]) < 0)
                ERROR("Upload failed: %s -> %s", tokens[1], tokens[2]);
            else
                SUCCESS("Upload completed: %s -> %s", tokens[1], tokens[2]);
            return 0;
        }

        ERROR("Usage: upload <local> <remote> OR upload -b <local> <remote>");
        return 0;
    }

    // -----------------------------------------------------------
    // DOWNLOAD (foreground)
    if (strcmp(cmd, "download") == 0) {
        if (n == 4 && strcmp(tokens[1], "-b") == 0) {
            startBackgroundDownload(tokens[2], tokens[3]);
            return 0;
        }

        if (n == 3) {
            if (downloadFile(sock, tokens[1], tokens[2]) < 0)
                ERROR("Download failed: %s -> %s", tokens[1], tokens[2]);
            else
                SUCCESS("Download completed: %s -> %s", tokens[1], tokens[2]);
            return 0;
        }

        ERROR("Usage: download <remote> <local> OR download -b <remote> <local>");
        return 0;
    }

    // -----------------------------------------------------------
    // EXIT
    if (strcmp(cmd, "exit") == 0) {
        if (hasActiveBackgroundProcesses()) {
            ERROR("Cannot exit: %d background transfer(s) still running", bgCount);
            printf("Wait for them to finish or use Ctrl+C\n");
            return 0;
        }

        sendSimpleCommand(sock, CMD_EXIT, NULL, NULL, NULL);
        return 1;
    }

    ERROR("Unknown command: %s", cmd);
    return 0;
}