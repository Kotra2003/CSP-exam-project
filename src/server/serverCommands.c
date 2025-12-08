#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>

#include "../../include/serverCommands.h"
#include "../../include/protocol.h"
#include "../../include/session.h"
#include "../../include/utils.h"
#include "../../include/fsOps.h"
#include "../../include/concurrency.h"
#include "../../include/network.h"

extern const char *gRootDir;

// ================================================================
// Helper: send simple response
// ================================================================
static void sendStatus(int clientFd, int status, int dataSize)
{
    ProtocolResponse res;
    res.status = status;
    res.dataSize = dataSize;
    sendResponse(clientFd, &res);
}

static void sendOk(int clientFd, int dataSize)
{
    sendStatus(clientFd, STATUS_OK, dataSize);
}

static void sendErrorMsg(int clientFd)
{
    sendStatus(clientFd, STATUS_ERROR, 0);
}

static int ensureLoggedIn(int clientFd, Session *session, const char *cmdName)
{
    if (!session->isLoggedIn) {
        printf("[%s] ERROR: user not logged in\n", cmdName);
        fflush(stdout);
        sendErrorMsg(clientFd);
        return 0;
    }
    return 1;
}

static void debugCommand(const char *name, ProtocolMessage *msg, Session *s)
{
    printf("[%s] cmd=%d, arg1='%s', arg2='%s', arg3='%s', loggedIn=%d\n",
        name,
        msg->command,
        msg->arg1,
        msg->arg2,
        msg->arg3,
        s ? s->isLoggedIn : -1);
    fflush(stdout);
}

// ================================================================
// DISPATCHER
// ================================================================
int processCommand(int clientFd, ProtocolMessage *msg, Session *session)
{
    switch (msg->command)
    {
        case CMD_LOGIN:       return handleLogin(clientFd, msg, session);
        case CMD_CREATE_USER: return handleCreateUser(clientFd, msg, session);
        case CMD_DELETE_USER: return handleDeleteUser(clientFd, msg, session);
        case CMD_CREATE:      return handleCreate(clientFd, msg, session);
        case CMD_CHMOD:       return handleChmod(clientFd, msg, session);
        case CMD_MOVE:        return handleMove(clientFd, msg, session);
        case CMD_CD:          return handleCd(clientFd, msg, session);
        case CMD_LIST:        return handleList(clientFd, msg, session);
        case CMD_READ:        return handleRead(clientFd, msg, session);
        case CMD_WRITE:       return handleWrite(clientFd, msg, session);
        case CMD_DELETE:      return handleDelete(clientFd, msg, session);
        case CMD_UPLOAD:      return handleUpload(clientFd, msg, session);
        case CMD_DOWNLOAD:    return handleDownload(clientFd, msg, session);
        case CMD_EXIT:        return 1;

        default:
            printf("[UNKNOWN] cmd=%d\n", msg->command);
            sendErrorMsg(clientFd);
            return 0;
    }
}

// ================================================================
// LOGIN
// ================================================================
int handleLogin(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("LOGIN", msg, session);

    if (session->isLoggedIn) {
        printf("[LOGIN] ERROR: already logged in.\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    if (msg->arg1[0] == '\0') {
        printf("[LOGIN] ERROR: missing username.\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    char homePath[PATH_SIZE];
    snprintf(homePath, PATH_SIZE, "%s/%s", gRootDir, msg->arg1);

    if (!fileExists(homePath)) {
        printf("[LOGIN] ERROR: no such user dir '%s'\n", homePath);
        sendErrorMsg(clientFd);
        return 0;
    }

    loginUser(session, gRootDir, msg->arg1);

    printf("[LOGIN] OK user='%s'\n", session->username);
    sendOk(clientFd, 0);
    return 0;
}

// ================================================================
// CREATE USER
// ================================================================
int handleCreateUser(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("CREATE_USER", msg, session);

    if (msg->arg1[0] == '\0' || msg->arg2[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    const char *username = msg->arg1;
    int permissions = (int)strtol(msg->arg2, NULL, 8);

    if (permissions <= 0) {
        printf("[CREATE_USER] ERROR invalid permissions\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    char homePath[PATH_SIZE];
    snprintf(homePath, PATH_SIZE, "%s/%s", gRootDir, username);

    if (fileExists(homePath)) {
        printf("[CREATE_USER] ERROR: '%s' exists\n", homePath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // 1) SYSTEM USER CREATION
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        sendErrorMsg(clientFd);
        return 0;
    }
    if (pid == 0) {
        execlp("adduser", "adduser",
               "--disabled-password",
               "--gecos", "",
               username,
               NULL);
        perror("execlp adduser");
        exit(1);
    }

    int status;
    waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        printf("[CREATE_USER] adduser failed\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    // 2) CREATE HOME DIRECTORY
    if (mkdir(homePath, permissions) < 0) {
        perror("mkdir");
        sendErrorMsg(clientFd);
        return 0;
    }

    // 3) SET PERMISSIONS
    if (chmod(homePath, permissions) < 0) {
        perror("chmod");
        sendErrorMsg(clientFd);
        return 0;
    }

    // 4) SET OWNER TO THIS USER + SHARED GROUP
    struct group *grp = getgrnam("csapgroup");
    if (!grp) {
        printf("[CREATE_USER] ERROR: group csapgroup not found\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    struct passwd *pwd = getpwnam(username);
    if (!pwd) {
        printf("[CREATE_USER] ERROR: passwd entry missing\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    if (chown(homePath, pwd->pw_uid, grp->gr_gid) < 0) {
        perror("chown");
        sendErrorMsg(clientFd);
        return 0;
    }

    printf("[CREATE_USER] OK user '%s' perms %o\n", username, permissions);
    sendOk(clientFd, 0);
    return 0;
}

// ================================================================
// CREATE FILE / DIR
// ================================================================
int handleCreate(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("CREATE", msg, session);

    if (!ensureLoggedIn(clientFd, session, "CREATE"))
        return 0;

    const char *pathArg = msg->arg1;
    const char *permArg = msg->arg2;
    const char *typeArg = msg->arg3;

    if (!pathArg[0] || !permArg[0] || !typeArg[0]) {
        sendErrorMsg(clientFd);
        return 0;
    }

    int permissions = (int)strtol(permArg, NULL, 8);
    int isDir = (strcmp(typeArg, "dir") == 0);

    char fullPath[PATH_SIZE];
    if (resolvePath(session, pathArg, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    if (!isInsideRoot(session->homeDir, fullPath)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    if (fileExists(fullPath)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    if (fsCreate(fullPath, permissions, isDir) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    sendOk(clientFd, 0);
    return 0;
}

// ================================================================
// CHMOD
// ================================================================
int handleChmod(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("CHMOD", msg, session);

    if (!ensureLoggedIn(clientFd, session, "CHMOD"))
        return 0;

    char fullPath[PATH_SIZE];

    if (msg->arg1[0] == '\0' || msg->arg2[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    int permissions = (int)strtol(msg->arg2, NULL, 8);

    if (resolvePath(session, msg->arg1, fullPath) < 0 ||
        !isInsideRoot(session->homeDir, fullPath) ||
        !fileExists(fullPath)) {

        sendErrorMsg(clientFd);
        return 0;
    }

    if (fsChmod(fullPath, permissions) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    sendOk(clientFd, 0);
    return 0;
}

// ================================================================
// MOVE
// ================================================================
int handleMove(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("MOVE", msg, session);

    if (!ensureLoggedIn(clientFd, session, "MOVE"))
        return 0;

    char src[PATH_SIZE], dst[PATH_SIZE];

    if (!msg->arg1[0] || !msg->arg2[0]) {
        sendErrorMsg(clientFd);
        return 0;
    }

    if (resolvePath(session, msg->arg1, src) < 0 ||
        resolvePath(session, msg->arg2, dst) < 0) {

        sendErrorMsg(clientFd);
        return 0;
    }

    if (!isInsideRoot(session->homeDir, src) ||
        !isInsideRoot(session->homeDir, dst) ||
        !fileExists(src) ||
        fileExists(dst)) {

        sendErrorMsg(clientFd);
        return 0;
    }

    if (acquireFileLock(src) < 0 || acquireFileLock(dst) < 0) {
        releaseFileLock(src);
        releaseFileLock(dst);
        sendErrorMsg(clientFd);
        return 0;
    }

    int ok = fsMove(src, dst);

    releaseFileLock(src);
    releaseFileLock(dst);

    if (ok < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    sendOk(clientFd, 0);
    return 0;
}

// ================================================================
// CD
// ================================================================
int handleCd(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("CD", msg, session);

    if (!ensureLoggedIn(clientFd, session, "CD"))
        return 0;

    char fullPath[PATH_SIZE];

    if (msg->arg1[0] == '\0') {
        strncpy(session->currentDir, session->homeDir, PATH_SIZE);
        sendOk(clientFd, 0);
        return 0;
    }

    if (resolvePath(session, msg->arg1, fullPath) < 0 ||
        !isInsideRoot(session->homeDir, fullPath)) {

        sendErrorMsg(clientFd);
        return 0;
    }

    struct stat st;
    if (stat(fullPath, &st) < 0 || !S_ISDIR(st.st_mode)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    strncpy(session->currentDir, fullPath, PATH_SIZE);
    sendOk(clientFd, 0);
    return 0;
}

// ================================================================
// LIST
// ================================================================
int handleList(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("LIST", msg, session);

    if (!ensureLoggedIn(clientFd, session, "LIST"))
        return 0;

    char fullPath[PATH_SIZE];
    char output[8192];
    output[0] = '\0';

    if (msg->arg1[0] == '\0') {
        strncpy(fullPath, session->currentDir, PATH_SIZE);
    } else {
        if (resolvePath(session, msg->arg1, fullPath) < 0) {
            sendErrorMsg(clientFd);
            return 0;
        }
    }

    if (!isInsideRoot(gRootDir, fullPath)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    DIR *dir = opendir(fullPath);
    if (!dir) {
        perror("opendir");
        sendErrorMsg(clientFd);
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        char entryPath[PATH_SIZE];
        snprintf(entryPath, PATH_SIZE, "%s/%s", fullPath, entry->d_name);

        struct stat st;
        if (stat(entryPath, &st) < 0)
            continue;

        int perms = st.st_mode & 0777;
        long size = (long)st.st_size;

        char line[512];
        snprintf(line, sizeof(line),
                 "%s  %03o  %ld\n",
                 entry->d_name,
                 perms,
                 size);

        strncat(output, line, sizeof(output) - strlen(output) - 1);
    }

    closedir(dir);

    sendOk(clientFd, strlen(output));
    if (strlen(output) > 0) {
        sendAll(clientFd, output, strlen(output));
    }

    return 0;
}

// ================================================================
// READ  (PDF: read <path>, read -offset=N <path>)
// ================================================================
int handleRead(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("READ", msg, session);

    if (!ensureLoggedIn(clientFd, session, "READ"))
        return 0;

    if (msg->arg1[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    char fullPath[PATH_SIZE];

    if (resolvePath(session, msg->arg1, fullPath) < 0 ||
        !isInsideRoot(session->homeDir, fullPath) ||
        !fileExists(fullPath)) {

        sendErrorMsg(clientFd);
        return 0;
    }

    int offset = 0;
    if (msg->arg2[0] != '\0') {
        offset = atoi(msg->arg2);
        if (offset < 0) offset = 0;
    }

    struct stat st;
    if (stat(fullPath, &st) < 0 || !S_ISREG(st.st_mode)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    int fileSize = (int)st.st_size;
    if (offset > fileSize) offset = fileSize;

    int toRead = fileSize - offset;

    if (acquireFileLock(fullPath) < 0) {
        printf("[READ] file in use '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    char *buffer = NULL;
    int readBytes = 0;

    if (toRead > 0) {
        buffer = malloc(toRead);
        if (!buffer) {
            releaseFileLock(fullPath);
            sendErrorMsg(clientFd);
            return 0;
        }

        readBytes = fsReadFile(fullPath, buffer, toRead, offset);
        if (readBytes < 0) {
            free(buffer);
            releaseFileLock(fullPath);
            sendErrorMsg(clientFd);
            return 0;
        }
    }

    releaseFileLock(fullPath);

    sendOk(clientFd, readBytes);

    if (readBytes > 0) {
        sendAll(clientFd, buffer, readBytes);
        free(buffer);
    }

    printf("[READ] %d bytes from '%s' (offset=%d)\n", readBytes, fullPath, offset);
    return 0;
}

// ================================================================
// WRITE  (PDF: write <path>, write -offset=N <path>)
// VARIJANTA 1: client šalje VELIČINU + PODATKE
// ================================================================
int handleWrite(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("WRITE", msg, session);

    if (!ensureLoggedIn(clientFd, session, "WRITE"))
        return 0;

    if (msg->arg1[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    char fullPath[PATH_SIZE];

    if (resolvePath(session, msg->arg1, fullPath) < 0 ||
        !isInsideRoot(session->homeDir, fullPath)) {

        sendErrorMsg(clientFd);
        return 0;
    }

    int offset = 0;
    if (msg->arg2[0] != '\0') {
        offset = atoi(msg->arg2);
        if (offset < 0) offset = 0;
    }

    if (acquireFileLock(fullPath) < 0) {
        printf("[WRITE] file in use '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // 1) Pošalji ACK da klijent može poslati veličinu + sadržaj
    sendOk(clientFd, 0);

    // 2) Primi int size
    int size = 0;
    if (recvAll(clientFd, &size, sizeof(int)) < 0 || size < 0) {
        printf("[WRITE] failed to receive size\n");
        releaseFileLock(fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    char *buffer = NULL;
    int written = 0;

    if (size > 0) {
        buffer = malloc(size);
        if (!buffer) {
            releaseFileLock(fullPath);
            sendErrorMsg(clientFd);
            return 0;
        }

        if (recvAll(clientFd, buffer, size) < 0) {
            printf("[WRITE] failed to receive data\n");
            free(buffer);
            releaseFileLock(fullPath);
            sendErrorMsg(clientFd);
            return 0;
        }

        // fsWriteFile treba da:
        //  - kreira fajl ako ne postoji (0700)
        //  - piše od zadanog offseta
        written = fsWriteFile(fullPath, buffer, size, offset);

        free(buffer);

        if (written < 0) {
            releaseFileLock(fullPath);
            sendErrorMsg(clientFd);
            return 0;
        }
    }

    releaseFileLock(fullPath);

    sendOk(clientFd, written);
    printf("[WRITE] %d bytes -> '%s' (offset=%d)\n", written, fullPath, offset);
    return 0;
}

// ================================================================
// DELETE (delete file or directory inside user's home)
// ================================================================
int handleDelete(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("DELETE", msg, session);

    if (!ensureLoggedIn(clientFd, session, "DELETE"))
        return 0;

    if (msg->arg1[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    char fullPath[PATH_SIZE];

    // Resolve relative to user directory
    if (resolvePath(session, msg->arg1, fullPath) < 0 ||
        !isInsideRoot(session->homeDir, fullPath)) {

        sendErrorMsg(clientFd);
        return 0;
    }

    // Check existence
    if (!fileExists(fullPath)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // Acquire lock to avoid races
    if (acquireFileLock(fullPath) < 0) {
        printf("[DELETE] file in use '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    int ok = removeRecursive(fullPath);

    releaseFileLock(fullPath);

    if (ok < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    printf("[DELETE] Removed '%s'\n", fullPath);
    sendOk(clientFd, 0);
    return 0;
}


// ================================================================
// UPLOAD
// ================================================================
int handleUpload(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("UPLOAD", msg, session);

    if (!ensureLoggedIn(clientFd, session, "UPLOAD"))
        return 0;

    char fullPath[PATH_SIZE];
    int size = atoi(msg->arg2);

    if (!msg->arg1[0] || size <= 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    if (resolvePath(session, msg->arg1, fullPath) < 0 ||
        !isInsideRoot(session->homeDir, fullPath)) {

        sendErrorMsg(clientFd);
        return 0;
    }

    if (acquireFileLock(fullPath) < 0) {
        printf("[UPLOAD] file in use '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    sendOk(clientFd, 0); // tell client to send data

    char *buffer = malloc(size);
    if (!buffer) {
        releaseFileLock(fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    if (recvAll(clientFd, buffer, size) < 0) {
        free(buffer);
        releaseFileLock(fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    int written = fsWriteFile(fullPath, buffer, size, 0);

    free(buffer);
    releaseFileLock(fullPath);

    if (written < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    sendOk(clientFd, written);
    return 0;
}

// ================================================================
// DOWNLOAD
// ================================================================
int handleDownload(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("DOWNLOAD", msg, session);

    if (!ensureLoggedIn(clientFd, session, "DOWNLOAD"))
        return 0;

    char fullPath[PATH_SIZE];

    if (!msg->arg1[0] ||
        resolvePath(session, msg->arg1, fullPath) < 0 ||
        !isInsideRoot(session->homeDir, fullPath)) {

        sendErrorMsg(clientFd);
        return 0;
    }

    struct stat st;
    if (stat(fullPath, &st) < 0 || !S_ISREG(st.st_mode)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    int size = (int)st.st_size;

    if (acquireFileLock(fullPath) < 0) {
        printf("[DOWNLOAD] file in use '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    char *buffer = malloc(size);
    if (!buffer) {
        releaseFileLock(fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    int readBytes = fsReadFile(fullPath, buffer, size, 0);

    releaseFileLock(fullPath);

    if (readBytes < 0) {
        free(buffer);
        sendErrorMsg(clientFd);
        return 0;
    }

    sendOk(clientFd, readBytes);
    sendAll(clientFd, buffer, readBytes);

    free(buffer);
    return 0;
}

// ================================================================
// DELETE USER
// ================================================================
int handleDeleteUser(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("DELETE_USER", msg, session);

    if (!session->isLoggedIn  || strcmp(session->username, "admin") != 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    if (msg->arg1[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    const char *target = msg->arg1;

    if (strcmp(target, "admin") == 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    char homePath[PATH_SIZE];
    snprintf(homePath, PATH_SIZE, "%s/%s", gRootDir, target);

    if (!fileExists(homePath)) {
        printf("[DELETE_USER] No such user dir: %s\n", homePath);
        sendErrorMsg(clientFd);
        return 0;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execlp("userdel", "userdel", "-f", target, NULL);
        perror("userdel");
        exit(1);
    }

    int status;
    waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        printf("[DELETE_USER] userdel failed\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    if (removeRecursive(homePath) < 0) {
        printf("[DELETE_USER] rm -rf failed: %s\n", homePath);
        sendErrorMsg(clientFd);
        return 0;
    }

    printf("[DELETE_USER] '%s' deleted successfully\n", target);
    sendOk(clientFd, 0);
    return 0;
}
