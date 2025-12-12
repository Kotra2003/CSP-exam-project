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
#include <errno.h>

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
    res.status   = status;
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
// Privilege helpers: privremeni root samo kad treba
// ================================================================
static int elevateToRoot(uid_t *old_euid)
{
    *old_euid = geteuid();

    // Pokušaj da postaneš root (radi samo ako je proces startan sa sudo ./server)
    if (seteuid(0) != 0) {
        perror("seteuid(0)");
        return -1;
    }
    return 0;
}

static void dropFromRoot(uid_t old_euid)
{
    if (seteuid(old_euid) != 0) {
        perror("seteuid(revert)");
        // Ako ovo faila, proces ostaje sa višim privilegijama – u realnom kodu bi ovdje
        // vjerovatno prekinuli proces. Za projekat je dovoljno da logujemo.
    }
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
// CREATE USER (sa privremenim root-om)
// ================================================================
int handleCreateUser(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("CREATE_USER", msg, session);

    // 1) Osnovne provjere koje ne traže root
    if (msg->arg1[0] == '\0' || msg->arg2[0] == '\0') {
        printf("[CREATE_USER] ERROR: missing <username> or <permissions>\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    const char *username  = msg->arg1;
    int permissions       = (int)strtol(msg->arg2, NULL, 8);

    if (permissions <= 0) {
        printf("[CREATE_USER] ERROR invalid permissions\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    char homePath[PATH_SIZE];
    snprintf(homePath, PATH_SIZE, "%s/%s", gRootDir, username);

    if (fileExists(homePath)) {
        printf("[CREATE_USER] ERROR: '%s' already exists\n", homePath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // 2) Pokušaj da privremeno postaneš root
    uid_t old_euid;
    if (elevateToRoot(&old_euid) < 0) {
        printf("[CREATE_USER] ERROR: server has no root privileges (run with sudo to create system users).\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    int error = 0;

    // 3) ROOT SEKCIJA: adduser, mkdir, chmod, chown
    do {
        // 3.1 create system user
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            error = 1;
            break;
        }
        if (pid == 0) {
            execlp("adduser", "adduser",
                   "--disabled-password",
                   "--gecos", "",
                   username,
                   NULL);
            perror("execlp adduser");
            _exit(1);
        }

        int status;
        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid");
            error = 1;
            break;
        }

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("[CREATE_USER] adduser failed (exit=%d)\n", WEXITSTATUS(status));
            error = 1;
            break;
        }

        // 3.2 create home directory inside gRootDir
        if (mkdir(homePath, permissions) < 0) {
            perror("mkdir");
            error = 1;
            break;
        }

        // 3.3 set permissions
        if (chmod(homePath, permissions) < 0) {
            perror("chmod");
            error = 1;
            break;
        }

        // 3.4 set owner and group
        struct group *grp = getgrnam("csapgroup");
        if (!grp) {
            printf("[CREATE_USER] ERROR: group 'csapgroup' not found\n");
            error = 1;
            break;
        }

        struct passwd *pwd = getpwnam(username);
        if (!pwd) {
            printf("[CREATE_USER] ERROR: passwd entry missing for '%s'\n", username);
            error = 1;
            break;
        }

        if (chown(homePath, pwd->pw_uid, grp->gr_gid) < 0) {
            perror("chown");
            error = 1;
            break;
        }

    } while (0);

    // 4) Obavezno vrati nazad stari EUID (čak i ako je bilo errova)
    dropFromRoot(old_euid);

    if (error) {
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

    if (!pathArg[0] || !permArg[0]) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // ============================================
    // VALIDACIJA PERMISSIONA (0–777 OKTALNO)
    // ============================================
    char *endptr;
    long perms = strtol(permArg, &endptr, 8);

    if (*endptr != '\0' || perms < 0 || perms > 0777) {
        printf("[CREATE] invalid permissions: %s\n", permArg);
        sendErrorMsg(clientFd);
        return 0;
    }

    int permissions = (int)perms;

    // ============================================
    // DA LI JE DIRECTORY?
    // ============================================
    int isDir = (strcmp(typeArg, "-d") == 0);

    char fullPath[PATH_SIZE];
    if (resolvePath(session, pathArg, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    if (!isInsideRoot(session->homeDir, fullPath) || fileExists(fullPath)) {
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

    // -----------------------------
    // Validate args exist
    // -----------------------------
    if (msg->arg1[0] == '\0' || msg->arg2[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    // -----------------------------
    // Validate permissions (0–777)
    // -----------------------------
    char *endptr;
    long perms = strtol(msg->arg2, &endptr, 8);

    if (*endptr != '\0' || perms < 0 || perms > 0777) {
        printf("[CHMOD] invalid permissions: %s\n", msg->arg2);
        sendErrorMsg(clientFd);
        return 0;
    }

    int permissions = (int)perms;

    // -----------------------------
    // Resolve full path
    // -----------------------------
    if (resolvePath(session, msg->arg1, fullPath) < 0 ||
        !isInsideRoot(session->homeDir, fullPath) ||
        !fileExists(fullPath)) {

        sendErrorMsg(clientFd);
        return 0;
    }

    // -----------------------------
    // Acquire lock (IMPORTANT)
    // -----------------------------
    if (acquireFileLock(fullPath) < 0) {
        printf("[CHMOD] file is locked: %s\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // -----------------------------
    // Do chmod
    // -----------------------------
    int ok = fsChmod(fullPath, permissions);

    // Release lock
    releaseFileLock(fullPath);

    if (ok < 0) {
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
        // cd bez argumenata - vrati na home
        strncpy(session->currentDir, session->homeDir, PATH_SIZE);
        
        // Vrati putanju za prikaz
        char displayPath[PATH_SIZE] = "/";
        sendOk(clientFd, strlen(displayPath));
        if (strlen(displayPath) > 0) {
            sendAll(clientFd, displayPath, strlen(displayPath));
        }
        return 0;
    }

    // Resolve path
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

    // Update session
    strncpy(session->currentDir, fullPath, PATH_SIZE);
    
    // Calculate display path (relative to home directory)
    char displayPath[PATH_SIZE];
    size_t homeLen = strlen(session->homeDir);
    
    if (strncmp(session->currentDir, session->homeDir, homeLen) == 0) {
        if (session->currentDir[homeLen] == '\0') {
            strcpy(displayPath, "/");
        } else if (session->currentDir[homeLen] == '/') {
            snprintf(displayPath, PATH_SIZE, "/%s", session->currentDir + homeLen + 1);
        } else {
            strcpy(displayPath, "/");
        }
    } else {
        strcpy(displayPath, "/");
    }
    
    // Send OK with the new display path
    sendOk(clientFd, strlen(displayPath));
    if (strlen(displayPath) > 0) {
        sendAll(clientFd, displayPath, strlen(displayPath));
    }
    
    printf("[CD] OK -> '%s' (display: '%s')\n", fullPath, displayPath);
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

    // ============================================
    // 1) Odredi putanju koja se lista
    // ============================================
    if (msg->arg1[0] == '\0') {
        // Bez argumenata -> trenutni direktorij
        strncpy(fullPath, session->currentDir, PATH_SIZE);
        fullPath[PATH_SIZE - 1] = '\0';
    }
    else if (msg->arg1[0] == '/') {
        // Apsolutna putanja unutar root-a
        // Primjer: /admin -> <rootDir>/admin
        //          /admin/docs -> <rootDir>/admin/docs
        snprintf(fullPath, PATH_SIZE, "%s/%s", gRootDir, msg->arg1 + 1);
    }
    else {
        // Relativna putanja -> koristi resolvePath
        if (resolvePath(session, msg->arg1, fullPath) < 0) {
            printf("[LIST] ERROR: resolvePath failed for '%s'\n", msg->arg1);
            sendErrorMsg(clientFd);
            return 0;
        }
    }

    // ============================================
    // 2) Provjeri da li je putanja unutar root-a
    // ============================================
    if (!isInsideRoot(gRootDir, fullPath)) {
        printf("[LIST] ERROR: Path outside root: '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // ============================================
    // 3) Provjeri da li direktorij postoji
    // ============================================
    struct stat st;
    if (stat(fullPath, &st) < 0) {
        printf("[LIST] ERROR: stat failed for '%s': %s\n", fullPath, strerror(errno));
        sendErrorMsg(clientFd);
        return 0;
    }

    if (!S_ISDIR(st.st_mode)) {
        printf("[LIST] ERROR: Not a directory: '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // ============================================
    // 4) OTVORI DIREKTORIJ
    // ============================================
    DIR *dir = opendir(fullPath);
    if (!dir) {
        printf("[LIST] ERROR: opendir failed for '%s': %s\n", fullPath, strerror(errno));
        sendErrorMsg(clientFd);
        return 0;
    }

    // HEADER
    strcat(output, "============================================================\n");
    strcat(output, "                         CONTENTS                          \n");
    strcat(output, "------------------------------------------------------------\n");
    strcat(output, " NAME                              PERMISSIONS     SIZE     \n");
    strcat(output, "------------------------------------------------------------\n");

    struct dirent *entry;
    int itemCount = 0;
    
    while ((entry = readdir(dir)) != NULL)
    {
        // Preskoči . i .. i .lock fajlove
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || strstr(entry->d_name, ".lock") != NULL)
            continue;

        char entryPath[PATH_SIZE];
        snprintf(entryPath, PATH_SIZE, "%s/%s", fullPath, entry->d_name);

        // Stat fajla/direktorijuma
        if (stat(entryPath, &st) < 0) {
            // Ako stat faila, nastavi sa sljedećim
            continue;
        }

        int  perms = st.st_mode & 0777;
        long size  = (long)st.st_size;
        int  isDir = S_ISDIR(st.st_mode);

        // Formatiraj ispis
        char line[512];
        if (isDir) {
            snprintf(line, sizeof(line), " %-30s [DIR]  %04o      %6ld\n",
                     entry->d_name, perms, size);
        } else {
            snprintf(line, sizeof(line), " %-30s [FILE] %04o      %6ld\n",
                     entry->d_name, perms, size);
        }

        strncat(output, line, sizeof(output) - strlen(output) - 1);
        itemCount++;
    }

    closedir(dir);

    // FOOTER
    strcat(output, "------------------------------------------------------------\n");
    
    char footer[128];
    snprintf(footer, sizeof(footer), " Total: %d item(s)\n", itemCount);
    strcat(output, footer);
    
    strcat(output, "============================================================\n");

    // DEBUG: ispiši šta šaljemo
    printf("[LIST] OK: Sending %ld bytes for path '%s'\n", strlen(output), fullPath);

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

    char *buffer   = NULL;
    int   readBytes = 0;

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
    int   written = 0;

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
    printf("[WRITE] %d bytes -> '%s' (offset=%d)\n", written, fullPath, offset);\
    printf("SERVER RECEIVED OFFSET = '%s'\n", msg->arg2);
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

    if (resolvePath(session, msg->arg1, fullPath) < 0 ||
        !isInsideRoot(session->homeDir, fullPath) ||
        !fileExists(fullPath)) {

        sendErrorMsg(clientFd);
        return 0;
    }

    // ----------------------------------------
    // Acquire lock (prevents deleting in-use files)
    // ----------------------------------------
    if (acquireFileLock(fullPath) < 0) {
        printf("[DELETE] file in use: %s\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // ----------------------------------------
    // Delete .lock file if exists
    // ----------------------------------------
    char lockPath[PATH_SIZE + 10];
    snprintf(lockPath, sizeof(lockPath), "%s.lock", fullPath);
    unlink(lockPath);  // ignore errors

    // ----------------------------------------
    // Delete file or directory recursively
    // ----------------------------------------
    int ok = removeRecursive(fullPath);

    releaseFileLock(fullPath);

    if (ok < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

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
// DELETE USER (sa privremenim root-om)
// ================================================================
int handleDeleteUser(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("DELETE_USER", msg, session);

    // 1) Samo admin smije brisati korisnike
    if (!session->isLoggedIn || strcmp(session->username, "admin") != 0) {
        printf("[DELETE_USER] ERROR: only 'admin' can delete users\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    // 2) Mora postojati ime korisnika za brisanje
    if (msg->arg1[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    const char *target = msg->arg1;

    // 3) Admin ne smije brisati sam sebe
    if (strcmp(target, "admin") == 0) {
        printf("[DELETE_USER] ERROR: admin account cannot be deleted\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    // 4) Složimo path do home foldera
    char homePath[PATH_SIZE];
    snprintf(homePath, PATH_SIZE, "%s/%s", gRootDir, target);

    if (!fileExists(homePath)) {
        printf("[DELETE_USER] No such user dir: %s\n", homePath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // 5) Privremeno digni privilegije na root
    uid_t old_euid;
    if (elevateToRoot(&old_euid) < 0) {
        printf("[DELETE_USER] ERROR: server not running with sudo.\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    int error = 0;

    // 6) userdel + remove directory
    do {
        // userdel
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); error = 1; break; }

        if (pid == 0) {
            execlp("userdel", "userdel", "-f", target, NULL);
            perror("userdel");
            _exit(1);
        }

        int status;
        if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); error = 1; break; }

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("[DELETE_USER] userdel failed\n");
            error = 1;
            break;
        }

        // rm -rf home dir
        if (removeRecursive(homePath) < 0) {
            printf("[DELETE_USER] rm -rf failed: %s\n", homePath);
            error = 1;
            break;
        }

    } while (0);

    // 7) Vrati privilegije nazad
    dropFromRoot(old_euid);

    if (error) {
        sendErrorMsg(clientFd);
        return 0;
    }

    printf("[DELETE_USER] '%s' deleted successfully\n", target);
    sendOk(clientFd, 0);
    return 0;
}
