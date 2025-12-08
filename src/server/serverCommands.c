#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/socket.h>

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

    if (msg->arg1[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    char path[PATH_SIZE];
    snprintf(path, PATH_SIZE, "%s/%s", gRootDir, msg->arg1);

    if (fileExists(path)) {
        printf("[CREATE_USER] ERROR exists '%s'\n", path);
        sendErrorMsg(clientFd);
        return 0;
    }

    if (mkdir(path, 0700) < 0) {
        perror("mkdir");
        sendErrorMsg(clientFd);
        return 0;
    }

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

    // STRIKTNI LOCK za oba fajla
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
    char buffer[4096];
    buffer[0] = '\0';

    if (msg->arg1[0] == '\0') {
        strncpy(fullPath, session->currentDir, PATH_SIZE);
    } else {
        if (resolvePath(session, msg->arg1, fullPath) < 0)
            return sendErrorMsg(clientFd), 0;
    }

    if (!isInsideRoot(session->homeDir, fullPath)) {
        return sendErrorMsg(clientFd), 0;
    }

    DIR *dir = opendir(fullPath);
    if (!dir) {
        return sendErrorMsg(clientFd), 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") &&
            strcmp(entry->d_name, "..")) {

            strcat(buffer, entry->d_name);
            strcat(buffer, "\n");
        }
    }
    closedir(dir);

    sendOk(clientFd, strlen(buffer));
    send(clientFd, buffer, strlen(buffer), 0);
    return 0;
}

// ================================================================
// READ
// ================================================================
int handleRead(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("READ", msg, session);

    if (!ensureLoggedIn(clientFd, session, "READ"))
        return 0;

    if (!msg->arg1[0] || !msg->arg2[0] || !msg->arg3[0])
        return sendErrorMsg(clientFd), 0;

    char fullPath[PATH_SIZE];
    if (resolvePath(session, msg->arg1, fullPath) < 0 ||
        !isInsideRoot(session->homeDir, fullPath) ||
        !fileExists(fullPath)) {

        return sendErrorMsg(clientFd), 0;
    }

    int offset = atoi(msg->arg2);
    int size = atoi(msg->arg3);
    if (size <= 0)
        return sendErrorMsg(clientFd), 0;

    // STRIKT LOCK
    if (acquireFileLock(fullPath) < 0) {
        printf("[READ] file locked '%s'\n", fullPath);
        return sendErrorMsg(clientFd), 0;
    }

    char *buffer = malloc(size);
    if (!buffer) {
        releaseFileLock(fullPath);
        return sendErrorMsg(clientFd), 0;
    }

    int readBytes = fsReadFile(fullPath, buffer, size, offset);
    if (readBytes < 0) {
        free(buffer);
        releaseFileLock(fullPath);
        return sendErrorMsg(clientFd), 0;
    }

    releaseFileLock(fullPath);

    sendOk(clientFd, readBytes);
    send(clientFd, buffer, readBytes, 0);

    free(buffer);
    return 0;
}

// ================================================================
// WRITE
// ================================================================
int handleWrite(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("WRITE", msg, session);

    if (!ensureLoggedIn(clientFd, session, "WRITE"))
        return 0;

    char fullPath[PATH_SIZE];

    if (!msg->arg1[0] || !msg->arg2[0] || !msg->arg3[0])
        return sendErrorMsg(clientFd), 0;

    int offset = atoi(msg->arg2);
    int size = atoi(msg->arg3);

    if (size <= 0)
        return sendErrorMsg(clientFd), 0;

    if (resolvePath(session, msg->arg1, fullPath) < 0 ||
        !isInsideRoot(session->homeDir, fullPath)) {

        return sendErrorMsg(clientFd), 0;
    }

    // STRICT EXCLUSIVE LOCK
    if (acquireFileLock(fullPath) < 0) {
        printf("[WRITE] file in use '%s'\n", fullPath);
        return sendErrorMsg(clientFd), 0;
    }

    // ACK before receiving data
    sendOk(clientFd, 0);

    char *buffer = malloc(size);
    if (!buffer) {
        releaseFileLock(fullPath);
        return sendErrorMsg(clientFd), 0;
    }

    if (recvAll(clientFd, buffer, size) < 0) {
        free(buffer);
        releaseFileLock(fullPath);
        return sendErrorMsg(clientFd), 0;
    }

    int written = fsWriteFile(fullPath, buffer, size, offset);

    free(buffer);
    releaseFileLock(fullPath);

    if (written < 0)
        return sendErrorMsg(clientFd), 0;

    sendOk(clientFd, written);
    return 0;
}

// ================================================================
// DELETE
// ================================================================
int handleDelete(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("DELETE", msg, session);

    if (!ensureLoggedIn(clientFd, session, "DELETE"))
        return 0;

    char fullPath[PATH_SIZE];

    if (!msg->arg1[0] ||
        resolvePath(session, msg->arg1, fullPath) < 0 ||
        !isInsideRoot(session->homeDir, fullPath)) {

        return sendErrorMsg(clientFd), 0;
    }

    struct stat st;
    if (stat(fullPath, &st) < 0)
        return sendErrorMsg(clientFd), 0;

    // If it's directory → ensure empty
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(fullPath);
        if (!dir) return sendErrorMsg(clientFd), 0;

        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
                closedir(dir);
                return sendErrorMsg(clientFd), 0;
            }
        }
        closedir(dir);
    }

    // STRICT LOCK
    if (acquireFileLock(fullPath) < 0) {
        printf("[DELETE] file in use '%s'\n", fullPath);
        return sendErrorMsg(clientFd), 0;
    }

    int ok = fsDelete(fullPath);

    releaseFileLock(fullPath);

    if (ok < 0)
        return sendErrorMsg(clientFd), 0;

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

    if (!msg->arg1[0] || size <= 0)
        return sendErrorMsg(clientFd), 0;

    if (resolvePath(session, msg->arg1, fullPath) < 0 ||
        !isInsideRoot(session->homeDir, fullPath)) {

        return sendErrorMsg(clientFd), 0;
    }

    // LOCK
    if (acquireFileLock(fullPath) < 0) {
        printf("[UPLOAD] file in use '%s'\n", fullPath);
        return sendErrorMsg(clientFd), 0;
    }

    sendOk(clientFd, 0); // tell client to send data

    char *buffer = malloc(size);
    if (!buffer) {
        releaseFileLock(fullPath);
        return sendErrorMsg(clientFd), 0;
    }

    if (recvAll(clientFd, buffer, size) < 0) {
        free(buffer);
        releaseFileLock(fullPath);
        return sendErrorMsg(clientFd), 0;
    }

    int written = fsWriteFile(fullPath, buffer, size, 0);

    free(buffer);
    releaseFileLock(fullPath);

    if (written < 0)
        return sendErrorMsg(clientFd), 0;

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

        return sendErrorMsg(clientFd), 0;
    }

    struct stat st;
    if (stat(fullPath, &st) < 0 || !S_ISREG(st.st_mode))
        return sendErrorMsg(clientFd), 0;

    int size = st.st_size;

    // LOCK
    if (acquireFileLock(fullPath) < 0) {
        printf("[DOWNLOAD] file in use '%s'\n", fullPath);
        return sendErrorMsg(clientFd), 0;
    }

    char *buffer = malloc(size);
    if (!buffer) {
        releaseFileLock(fullPath);
        return sendErrorMsg(clientFd), 0;
    }

    int readBytes = fsReadFile(fullPath, buffer, size, 0);

    releaseFileLock(fullPath);

    if (readBytes < 0) {
        free(buffer);
        return sendErrorMsg(clientFd), 0;
    }

    sendOk(clientFd, readBytes);
    send(clientFd, buffer, readBytes, 0);

    free(buffer);
    return 0;
}
