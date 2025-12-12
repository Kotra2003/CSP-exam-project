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
// External upload/download from networkClient.c
// ============================================================================
extern int uploadFile(int sock, const char *localPath, const char *remotePath);
extern int downloadFile(int sock, const char *remotePath, const char *localPath);

// ============================================================================
// Globals for background processes and current path
// ============================================================================
static const char *g_ip = NULL;
static int g_port = 0;
static char g_username[64] = "";      // store current logged user
static char g_currentPath[PATH_SIZE] = "/";

// ---------------------------------------------------------------------------
// Background process table
// ---------------------------------------------------------------------------
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
        printf("[ERROR] No response from server\n");
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
        printf("[BG] Login failed for user '%s'\n", g_username);
        return -1;
    }

    return 0;
}

// ============================================================================
// BACKGROUND UPLOAD
// ============================================================================
static void startBackgroundUpload(const char *local, const char *remote)
{
    pid_t pid = fork();
    if (pid < 0) {
        printf("[ERROR] Cannot fork background upload\n");
        return;
    }

    if (pid > 0) {
        registerBackgroundProcess(pid);
        printf("[Background] Upload started (PID=%d)\n", pid);
        fflush(stdout);
        return;
    }

    // CHILD PROCESS
    close(STDIN_FILENO);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    int bgSock = connectToServer(g_ip, g_port);
    if (bgSock < 0) {
        printf("[BG UPLOAD] Failed to connect\n");
        _exit(1);
    }

    if (backgroundLogin(bgSock) < 0) {
        close(bgSock);
        _exit(1);
    }

    printf("[BG UPLOAD] Started PID=%d: %s -> %s\n", getpid(), local, remote);
    sleep(5);

    int result = uploadFile(bgSock, local, remote);
    close(bgSock);

    if (result == 0) {
        printf("[Background] Command: upload %s %s concluded\n", remote, local);
    } else {
        printf("[Background] Command: upload %s %s FAILED\n", remote, local);
    }

    fflush(stdout);
    _exit(0);
}

// ============================================================================
// BACKGROUND DOWNLOAD
// ============================================================================
static void startBackgroundDownload(const char *remote, const char *local)
{
    pid_t pid = fork();
    if (pid < 0) {
        printf("[ERROR] Cannot fork background download\n");
        return;
    }

    if (pid > 0) {
        registerBackgroundProcess(pid);
        printf("[Background] Download started (PID=%d)\n", pid);
        fflush(stdout);
        return;
    }

    // CHILD PROCESS
    close(STDIN_FILENO);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    int bgSock = connectToServer(g_ip, g_port);
    if (bgSock < 0) {
        printf("[BG DOWNLOAD] Failed to connect\n");
        _exit(1);
    }

    if (backgroundLogin(bgSock) < 0) {
        close(bgSock);
        _exit(1);
    }

    printf("[BG DOWNLOAD] Started PID=%d: %s -> %s\n", getpid(), remote, local);
    sleep(5);

    int result = downloadFile(bgSock, remote, local);
    close(bgSock);

    if (result == 0) {
        printf("[Background] Command: download %s %s concluded\n", remote, local);
    } else {
        printf("[Background] Command: download %s %s FAILED\n", remote, local);
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
        if (n < 2) { printf("Usage: login <username>\n"); return 0; }

        if (sendSimpleCommand(sock, CMD_LOGIN, tokens[1], NULL, NULL) >= 0) {
            printf("Logged in as %s\n", tokens[1]);
            strncpy(g_username, tokens[1], sizeof(g_username));
            strcpy(g_currentPath, "/");  // Reset putanje
        }
        else {
            printf("[ERROR] login failed\n");
        }
        return 0;
    }

    // -----------------------------------------------------------
    // CREATE USER
    if (strcmp(cmd, "create_user") == 0) {
        if (n < 3) { printf("Usage: create_user <username> <permissions>\n"); return 0; }
        if (sendSimpleCommand(sock, CMD_CREATE_USER, tokens[1], tokens[2], NULL) >= 0)
            printf("User created.\n");
        else
            printf("[ERROR] create_user failed\n");
        return 0;
    }

    // -----------------------------------------------------------
    // DELETE USER
    if (strcmp(cmd, "delete_user") == 0) {
        if (n < 2) { printf("Usage: delete_user <username>\n"); return 0; }
        if (sendSimpleCommand(sock, CMD_DELETE_USER, tokens[1], NULL, NULL) >= 0)
            printf("User deleted.\n");
        else
            printf("[ERROR] delete_user failed\n");
        return 0;
    }

    // -----------------------------------------------------------
    // CD (sa primanjem nove putanje od servera)
    if (strcmp(cmd, "cd") == 0) {
        if (n < 2) { printf("Usage: cd <directory>\n"); return 0; }
        
        ProtocolMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.command = CMD_CD;
        strncpy(msg.arg1, tokens[1], ARG_SIZE);
        
        sendMessage(sock, &msg);
        
        ProtocolResponse res;
        if (receiveResponse(sock, &res) < 0) {
            printf("[ERROR] No response from server\n");
            return 0;
        }
        
        if (res.status != STATUS_OK) {
            printf("[ERROR] cd failed\n");
            return 0;
        }
        
        // Server šalje novu putanju
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
            printf("[ERROR] list failed\n");
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
            printf("Usage: create <path> <permissions> [-d]\n");
            return 0;
        }

        const char *path = tokens[1];
        const char *perm = tokens[2];
        const char *flag = (n == 4 ? tokens[3] : "");

        if (n == 4 && strcmp(flag, "-d") != 0) {
            printf("Usage: create <path> <permissions> [-d]\n");
            return 0;
        }

        if (sendSimpleCommand(sock, CMD_CREATE, path, perm, flag) < 0)
            printf("[ERROR] create failed\n");

        return 0;
    }

    // -----------------------------------------------------------
    // CHMOD
    if (strcmp(cmd, "chmod") == 0) {
        if (n < 3) { printf("Usage: chmod <path> <permissions>\n"); return 0; }
        if (sendSimpleCommand(sock, CMD_CHMOD, tokens[1], tokens[2], NULL) < 0)
            printf("[ERROR] chmod failed\n");
        return 0;
    }

    // -----------------------------------------------------------
    // MOVE
    if (strcmp(cmd, "move") == 0) {
        if (n < 3) { printf("Usage: move <src> <dst>\n"); return 0; }
        if (sendSimpleCommand(sock, CMD_MOVE, tokens[1], tokens[2], NULL) < 0)
            printf("[ERROR] move failed\n");
        return 0;
    }

    // -----------------------------------------------------------
    // DELETE
    if (strcmp(cmd, "delete") == 0) {
        if (n < 2) { printf("Usage: delete <path>\n"); return 0; }
        if (sendSimpleCommand(sock, CMD_DELETE, tokens[1], NULL, NULL) < 0)
            printf("[ERROR] delete failed\n");
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
            printf("Usage: read <path>\n       read -offset=N <path>\n");
            return 0;
        }

        sendMessage(sock, &msg);

        ProtocolResponse res;
        if (receiveResponse(sock, &res) < 0 || res.status != STATUS_OK) {
            printf("[ERROR] read failed\n");
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
            printf("Usage: write <path>\n       write -offset=N <path>\n");
            return 0;
        }

        sendMessage(sock, &msg);

        ProtocolResponse ack;
        if (receiveResponse(sock, &ack) < 0 || ack.status != STATUS_OK) {
            printf("[ERROR] write rejected\n");
            return 0;
        }

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

        int size = total;
        sendAll(sock, &size, sizeof(int));
        if (size > 0)
            sendAll(sock, buffer, size);

        free(buffer);

        ProtocolResponse fin;
        receiveResponse(sock, &fin);

        if (fin.status == STATUS_OK)
            printf("[OK] Wrote %d bytes\n", fin.dataSize);
        else
            printf("[ERROR] write failed\n");

        return 0;
    }

    // -----------------------------------------------------------
    // UPLOAD
    if (strcmp(cmd, "upload") == 0) {
        if (n == 4 && strcmp(tokens[1], "-b") == 0) {
            startBackgroundUpload(tokens[2], tokens[3]);
            return 0;
        }

        if (n == 3) {
            if (uploadFile(sock, tokens[1], tokens[2]) < 0)
                printf("[ERROR] upload failed\n");
            return 0;
        }

        printf("Usage:\n upload <local> <remote>\n upload -b <local> <remote>\n");
        return 0;
    }

    // -----------------------------------------------------------
    // DOWNLOAD
    if (strcmp(cmd, "download") == 0) {
        if (n == 4 && strcmp(tokens[1], "-b") == 0) {
            startBackgroundDownload(tokens[2], tokens[3]);
            return 0;
        }

        if (n == 3) {
            if (downloadFile(sock, tokens[1], tokens[2]) < 0)
                printf("[ERROR] download failed\n");
            return 0;
        }

        printf("Usage:\n download <remote> <local>\n download -b <remote> <local>\n");
        return 0;
    }

    // -----------------------------------------------------------
    // EXIT (BLOKIRAN DOK IMA BG PROCESA, ali Ctrl+C radi)
    // -----------------------------------------------------------
    if (strcmp(cmd, "exit") == 0) {
        if (hasActiveBackgroundProcesses()) {
            printf("\n[ERROR] Cannot exit: %d background transfer(s) still running:\n", bgCount);
            
            for (int i = 0; i < bgCount; i++) {
                printf("  - PID %d\n", bgPids[i]);
            }
            
            printf("\nWait for them to finish or use Ctrl+C to force exit.\n");
            return 0;  // BLOKIRAJ exit komandu
        }

        sendSimpleCommand(sock, CMD_EXIT, NULL, NULL, NULL);
        return 1;
    }

    printf("Unknown command: %s\n", cmd);
    return 0;
}