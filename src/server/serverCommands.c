#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include "../../include/serverCommands.h"
#include "../../include/protocol.h"
#include "../../include/session.h"
#include "../../include/utils.h"
#include "../../include/fsOps.h"
#include "../../include/concurrency.h"


// Decide which command has been received
int processCommand(int clientFd, ProtocolMessage *msg, Session *session)
{
    switch (msg->command) {

        case CMD_LOGIN:
            return handleLogin(clientFd, msg, session);

        case CMD_CREATE_USER:
            return handleCreateUser(clientFd, msg, session);

        case CMD_CREATE:
            return handleCreate(clientFd, msg, session);

        case CMD_CHMOD:
            return handleChmod(clientFd, msg, session);

        case CMD_MOVE:
            return handleMove(clientFd, msg, session);

        case CMD_CD:
            return handleCd(clientFd, msg, session);

        case CMD_LIST:
            return handleList(clientFd, msg, session);

        case CMD_READ:
            return handleRead(clientFd, msg, session);

        case CMD_WRITE:
            return handleWrite(clientFd, msg, session);

        case CMD_DELETE:
            return handleDelete(clientFd, msg, session);

        case CMD_EXIT:
            // return 1 so serverMain knows to close the connection
            return 1;

        default:
            printf("Unknown command received.\n");
            return 0;
    }
}

extern const char *gRootDir;

//---------------------------------LONGIN---------------------------------
int handleLogin(int clientFd, ProtocolMessage *msg, Session *session)
{
    ProtocolResponse res;
    char username[USERNAME_SIZE];
    char homePath[PATH_SIZE];

    // If already logged in, do not allow another login
    if (session->isLoggedIn) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check if username argument is not empty
    if (msg->arg1[0] == '\0') {
        // No username provided
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Copy username from message
    strncpy(username, msg->arg1, USERNAME_SIZE);
    username[USERNAME_SIZE - 1] = '\0';  // ensure null-terminated

    // Build home directory path: <rootDir>/<username>
    snprintf(homePath, PATH_SIZE, "%s/%s", gRootDir, username);

    // Check if user home directory exists
    if (!fileExists(homePath)) {
        // User does not exist (must be created with create_user)
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Initialize session for this user
    // This will set username, homeDir and currentDir
    loginUser(session, gRootDir, username);

    // Send OK response to client
    res.status = STATUS_OK;
    res.dataSize = 0;
    sendResponse(clientFd, &res);

    return 0;
}

//------------------------------------------------------------------------

//-------------------------------CREATE USER-------------------------------

int handleCreateUser(int clientFd, ProtocolMessage *msg, Session *session)
{
    ProtocolResponse res;

    // Username is in arg1
    if (msg->arg1[0] == '\0') {
        // Username missing
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    char username[USERNAME_SIZE];
    char path[PATH_SIZE];

    // Copy username
    strncpy(username, msg->arg1, USERNAME_SIZE);
    username[USERNAME_SIZE - 1] = '\0';

    // Build path like this one <rootDir>/<username>
    snprintf(path, PATH_SIZE, "%s/%s", gRootDir, username);

    // Check if user already exists
    if (fileExists(path)) {
        // User already exists → cannot create
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Try to create directory for user
    if (mkdir(path, 0700) < 0) {
        // Some OS error occurred
        perror("mkdir");
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Success – user created
    res.status = STATUS_OK;
    res.dataSize = 0;
    sendResponse(clientFd, &res);

    return 0;
}

//------------------------------------------------------------------------

//---------------------------------CREATE---------------------------------

int handleCreate(int clientFd, ProtocolMessage *msg, Session *session)
{
    ProtocolResponse res;
    char fullPath[PATH_SIZE];

    // Must be logged in
    if (!session->isLoggedIn) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Extract arguments
    const char *pathArg = msg->arg1;
    const char *permArg = msg->arg2;
    const char *typeArg = msg->arg3;

    // Check if path provided
    if (pathArg[0] == '\0' || permArg[0] == '\0' || typeArg[0] == '\0') {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check if permissions are numeric (Because it can only be 755 or 57.. but not a string)
    if (!isNumeric(permArg)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Convert permissions string to int base 8 (octal) (This can be done only after we are sure string is numeric only)
    int permissions = strtol(permArg, NULL, 8);

    // Determine if directory or file (if none then error)
    int isDirectory = 0;
    if (strcmp(typeArg, "dir") == 0) {
        isDirectory = 1;
    } else if (strcmp(typeArg, "file") != 0) {
        // Invalid type
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Build full path from session
    if (resolvePath(session, pathArg, fullPath) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check sandbox (must stay inside user's home directory)
    if (!isInsideRoot(session->homeDir, fullPath)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check if file already exists
    if (fileExists(fullPath)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Create file or directory
    if (fsCreate(fullPath, permissions, isDirectory) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Success
    res.status = STATUS_OK;
    res.dataSize = 0;
    sendResponse(clientFd, &res);

    return 0;
}

//------------------------------------------------------------------------

//--------------------------------CHMOD-----------------------------------

int handleChmod(int clientFd, ProtocolMessage *msg, Session *session)
{
    ProtocolResponse res;
    char fullPath[PATH_SIZE];

    // Must be logged in
    if (!session->isLoggedIn) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    const char *pathArg = msg->arg1;
    const char *permArg = msg->arg2;

    // Check missing arguments
    if (pathArg[0] == '\0' || permArg[0] == '\0') {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Permissions must be numeric
    if (!isNumeric(permArg)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Convert permissions from string to octal
    int permissions = strtol(permArg, NULL, 8);

    // Resolve path
    if (resolvePath(session, pathArg, fullPath) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Sandbox check — user CANNOT chmod outside his home
    if (!isInsideRoot(session->homeDir, fullPath)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check if file exists
    if (!fileExists(fullPath)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Apply chmod
    if (fsChmod(fullPath, permissions) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Success
    res.status = STATUS_OK;
    res.dataSize = 0;
    sendResponse(clientFd, &res);

    return 0;
}

//------------------------------------------------------------------------

//---------------------------------MOVE-----------------------------------

int handleMove(int clientFd, ProtocolMessage *msg, Session *session)
{
    ProtocolResponse res;
    char srcFull[PATH_SIZE];
    char dstFull[PATH_SIZE];

    // Must be logged in
    if (!session->isLoggedIn) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    const char *srcArg = msg->arg1;
    const char *dstArg = msg->arg2;

    // Check arguments
    if (srcArg[0] == '\0' || dstArg[0] == '\0') {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Resolve source path
    if (resolvePath(session, srcArg, srcFull) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Resolve destination path
    if (resolvePath(session, dstArg, dstFull) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Both paths MUST be inside user's home directory
    if (!isInsideRoot(session->homeDir, srcFull) ||
        !isInsideRoot(session->homeDir, dstFull)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check if source exists
    if (!fileExists(srcFull)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Destination MUST NOT already exist
    if (fileExists(dstFull)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Acquire write locks on both source and destination
    if (acquireWriteLock(srcFull) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    if (acquireWriteLock(dstFull) < 0) {
        releaseWriteLock(srcFull);
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Perform move (rename)
    if (fsMove(srcFull, dstFull) < 0) {
        releaseWriteLock(srcFull);
        releaseWriteLock(dstFull);

        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Release locks after success
    releaseWriteLock(srcFull);
    releaseWriteLock(dstFull);

    // Success
    res.status = STATUS_OK;
    res.dataSize = 0;
    sendResponse(clientFd, &res);

    return 0;
}

//-------------------------------------------------------------------------

//-----------------------------------CD------------------------------------

int handleCd(int clientFd, ProtocolMessage *msg, Session *session)
{
    ProtocolResponse res;
    char fullPath[PATH_SIZE];

    // User must be logged in to use cd
    if (!session->isLoggedIn) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // We always need argument or path for cd (if we don't have it we will just be returned to home)
    if (msg->arg1[0] == '\0') {
        strncpy(session->currentDir, session->homeDir, PATH_SIZE);
        res.status = STATUS_OK;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Build full path according to session rules
    if (resolvePath(session, msg->arg1, fullPath) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check sandbox restriction (must stay inside home directory)
    if (!isInsideRoot(session->homeDir, fullPath)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check if the directory exists
    struct stat st;
    if (stat(fullPath, &st) < 0) {  // If we don't get any info about directory than directory dosen't exist
        // Directory does not exist
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check if it is a directory (not a file)
    if (!S_ISDIR(st.st_mode)) {     // Checking mode of file to know if it's a directory
        // Not a directory
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Success — change current directory
    strncpy(session->currentDir, fullPath, PATH_SIZE);

    res.status = STATUS_OK;
    res.dataSize = 0;
    sendResponse(clientFd, &res);

    return 0;
}

//------------------------------------------------------------------------

//------------------------------LIST--------------------------------------

int handleList(int clientFd, ProtocolMessage *msg, Session *session)
{
    ProtocolResponse res;
    char fullPath[PATH_SIZE];
    char output[4096];   // enough for listing
    output[0] = '\0';

    // User must be logged in
    if (!session->isLoggedIn) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // If no argument → list currentDir
    if (msg->arg1[0] == '\0') {
        strncpy(fullPath, session->currentDir, PATH_SIZE);
    } else {
        // Build full path
        if (resolvePath(session, msg->arg1, fullPath) < 0) {
            res.status = STATUS_ERROR;
            res.dataSize = 0;
            sendResponse(clientFd, &res);
            return 0;
        }
    }

    // Check root-level sandbox (list can go anywhere under root)
    if (!isInsideRoot(gRootDir, fullPath)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check directory exists
    struct stat st;
    if (stat(fullPath, &st) < 0 || !S_ISDIR(st.st_mode)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Open directory
    DIR *dir = opendir(fullPath);
    if (!dir) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Read all entries
    struct dirent *entry;       // Like struct that can point to the file and be used for listing
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        strcat(output, entry->d_name);
        strcat(output, "\n");
    }

    closedir(dir);

    // Send OK + data
    res.status = STATUS_OK;
    res.dataSize = strlen(output);

    sendResponse(clientFd, &res);

    // Now send the actual listing text
    if (res.dataSize > 0)
        send(clientFd, output, res.dataSize, 0);

    return 0;
}

//------------------------------------------------------------------------

//------------------------------READ--------------------------------------

int handleRead(int clientFd, ProtocolMessage *msg, Session *session)
{
    ProtocolResponse res;
    char fullPath[PATH_SIZE];

    // Must be logged in
    if (!session->isLoggedIn) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    const char *pathArg   = msg->arg1;
    const char *offsetArg = msg->arg2;
    const char *sizeArg   = msg->arg3;

    // Check arguments
    if (pathArg[0] == '\0' ||
        offsetArg[0] == '\0' ||
        sizeArg[0] == '\0') {

        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check numeric arguments
    if (!isNumeric(offsetArg) || !isNumeric(sizeArg)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    int offset = atoi(offsetArg);
    int size   = atoi(sizeArg);

    if (size <= 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Resolve path
    if (resolvePath(session, pathArg, fullPath) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Must be inside user's home
    if (!isInsideRoot(session->homeDir, fullPath)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check existence
    if (!fileExists(fullPath)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Acquire read lock
    if (acquireReadLock(fullPath) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Allocate buffer for reading
    char *buffer = malloc(size);
    if (!buffer) {
        releaseReadLock(fullPath);
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    int bytesRead = fsReadFile(fullPath, buffer, size, offset);

    if (bytesRead < 0) {
        free(buffer);
        releaseReadLock(fullPath);

        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Release read lock (reading is done)
    releaseReadLock(fullPath);

    res.status = STATUS_OK;
    res.dataSize = bytesRead;

    sendResponse(clientFd, &res);

    // Send file data
    if (bytesRead > 0) {
        send(clientFd, buffer, bytesRead, 0);
    }

    free(buffer);
    return 0;
}

//------------------------------------------------------------------------

//-------------------------------WRITE------------------------------------

int handleWrite(int clientFd, ProtocolMessage *msg, Session *session)
{
    ProtocolResponse res;
    char fullPath[PATH_SIZE];

    // User must be logged in
    if (!session->isLoggedIn) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    const char *pathArg   = msg->arg1;
    const char *offsetArg = msg->arg2;
    const char *sizeArg   = msg->arg3;

    // Arguments must be present
    if (pathArg[0] == '\0' ||
        offsetArg[0] == '\0' ||
        sizeArg[0] == '\0') {

        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Offset and size must be numeric
    if (!isNumeric(offsetArg) || !isNumeric(sizeArg)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    int offset = atoi(offsetArg);
    int size   = atoi(sizeArg);

    if (size <= 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Resolve path
    if (resolvePath(session, pathArg, fullPath) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Sandbox: must stay inside user's home directory
    if (!isInsideRoot(session->homeDir, fullPath)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Acquire WRITE lock
    if (acquireWriteLock(fullPath) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Allocate buffer for incoming data
    char *buffer = malloc(size);
    if (!buffer) {
        releaseWriteLock(fullPath);

        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Receive raw data from client
    int received = recvAll(clientFd, buffer, size);
    if (received < 0) {
        free(buffer);
        releaseWriteLock(fullPath);

        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Perform write
    int written = fsWriteFile(fullPath, buffer, size, offset);

    free(buffer);
    releaseWriteLock(fullPath);

    if (written < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Success
    res.status = STATUS_OK;
    res.dataSize = written;
    sendResponse(clientFd, &res);

    return 0;
}

//------------------------------------------------------------------------

//------------------------------DELETE------------------------------------

int handleDelete(int clientFd, ProtocolMessage *msg, Session *session)
{
    ProtocolResponse res;
    char fullPath[PATH_SIZE];

    // Must be logged in
    if (!session->isLoggedIn) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    const char *pathArg = msg->arg1;

    // Check path provided
    if (pathArg[0] == '\0') {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Resolve path
    if (resolvePath(session, pathArg, fullPath) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Delete only inside user's home folder
    if (!isInsideRoot(session->homeDir, fullPath)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check existence
    struct stat st;
    if (stat(fullPath, &st) < 0) {
        // File does not exist
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // If it is a directory, check if empty
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(fullPath);
        if (!dir) {
            res.status = STATUS_ERROR;
            res.dataSize = 0;
            sendResponse(clientFd, &res);
            return 0;
        }

        struct dirent *entry;
        int notEmpty = 0;

        while ((entry = readdir(dir)) != NULL) {
            // We will jump over . and .. because they exist in every file
            if (strcmp(entry->d_name, ".") != 0 &&
                strcmp(entry->d_name, "..") != 0) {
                notEmpty = 1;
                break;
            }
        }

        closedir(dir);

        if (notEmpty) {
            // Directory not empty
            res.status = STATUS_ERROR;
            res.dataSize = 0;
            sendResponse(clientFd, &res);
            return 0;
        }
    }

    // Acquire write lock
    if (acquireWriteLock(fullPath) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Delete file or directory
    if (fsDelete(fullPath) < 0) {
        // Release lock before returning
        releaseWriteLock(fullPath);

        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Release lock after we have deleted file
    releaseWriteLock(fullPath);

    // Success
    res.status = STATUS_OK;
    res.dataSize = 0;
    sendResponse(clientFd, &res);

    return 0;
}

//------------------------------------------------------------------------

//------------------------------UPLOAD------------------------------------

int handleUpload(int clientFd, ProtocolMessage *msg, Session *session)
{
    ProtocolResponse res;
    char fullPath[PATH_SIZE];

    // Must be logged in
    if (!session->isLoggedIn) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    const char *pathArg = msg->arg1;
    const char *sizeArg = msg->arg2;

    // Check args
    if (pathArg[0] == '\0' || sizeArg[0] == '\0') {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Size must be numeric
    if (!isNumeric(sizeArg)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    int size = atoi(sizeArg);
    if (size <= 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Resolve target path
    if (resolvePath(session, pathArg, fullPath) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Must be inside user's home directory
    if (!isInsideRoot(session->homeDir, fullPath)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Acquire WRITE lock
    if (acquireWriteLock(fullPath) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Tell client to send data
    res.status = STATUS_OK;
    res.dataSize = 0;
    sendResponse(clientFd, &res);

    // Allocate buffer for file data
    char *buffer = malloc(size);
    if (!buffer) {
        releaseWriteLock(fullPath);
        return 0;
    }

    // Receive raw data
    int received = recvAll(clientFd, buffer, size);
    if (received < 0) {
        free(buffer);
        releaseWriteLock(fullPath);
        return 0;
    }

    // Write file (overwrite existing file if it exists)
    int written = fsWriteFile(fullPath, buffer, size, 0);

    free(buffer);
    releaseWriteLock(fullPath);

    if (written < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Success
    res.status = STATUS_OK;
    res.dataSize = written;
    sendResponse(clientFd, &res);

    return 0;
}

//------------------------------------------------------------------------

//-----------------------------DOWNLOAD-----------------------------------

int handleDownload(int clientFd, ProtocolMessage *msg, Session *session)
{
    ProtocolResponse res;
    char fullPath[PATH_SIZE];

    // Must be logged in
    if (!session->isLoggedIn) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    const char *pathArg = msg->arg1;

    if (pathArg[0] == '\0') {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Build absolute path
    if (resolvePath(session, pathArg, fullPath) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // User can download only his own files
    if (!isInsideRoot(session->homeDir, fullPath)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Check existence and get file size
    // Also we are checking if it's a regular file not directory
    struct stat st;
    if (stat(fullPath, &st) < 0 || !S_ISREG(st.st_mode)) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    int fileSize = st.st_size;

    // Acquire read lock
    if (acquireReadLock(fullPath) < 0) {
        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Allocate buffer
    char *buffer = malloc(fileSize);
    if (!buffer) {
        releaseReadLock(fullPath);
        return 0;
    }

    // Read file
    int bytesRead = fsReadFile(fullPath, buffer, fileSize, 0);
    if (bytesRead < 0) {
        free(buffer);
        releaseReadLock(fullPath);

        res.status = STATUS_ERROR;
        res.dataSize = 0;
        sendResponse(clientFd, &res);
        return 0;
    }

    // Release lock after read
    releaseReadLock(fullPath);

    // Send response header
    res.status = STATUS_OK;
    res.dataSize = bytesRead;

    sendResponse(clientFd, &res);

    // Send raw data
    send(clientFd, buffer, bytesRead, 0);

    free(buffer);
    return 0;
}

//------------------------------------------------------------------------