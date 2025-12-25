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
#include <ctype.h>

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

// ================================================================
// Session / debug helpers
// ================================================================
static int ensureLoggedIn(int clientFd, Session *session, const char *cmdName)
{
    if (!session->isLoggedIn) {
        printf("[%s] ERROR: user not logged in (please login first)\n", cmdName);
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
// Privilege helpers: temporary root only when required
// ================================================================
static int elevateToRoot(uid_t *old_euid)
{
    *old_euid = geteuid();

    if (seteuid(0) != 0) {
        perror("[PRIV] ERROR: seteuid(0) failed (server not started with sudo)");
        return -1;
    }
    return 0;
}

static void dropFromRoot(uid_t old_euid)
{
    if (seteuid(old_euid) != 0) {
        perror("[PRIV] ERROR: failed to drop root privileges");
    }
}

// ================================================================
// DISPATCHER
// ================================================================
int processCommand(int clientFd, ProtocolMessage *msg, Session *session)
{
    switch (msg->command)
    {
        case CMD_LOGIN:
            return handleLogin(clientFd, msg, session);

        case CMD_CREATE_USER:
            return handleCreateUser(clientFd, msg, session);

        case CMD_DELETE_USER:
            return handleDeleteUser(clientFd, msg, session);

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

        case CMD_UPLOAD:
            return handleUpload(clientFd, msg, session);

        case CMD_DOWNLOAD:
            return handleDownload(clientFd, msg, session);

        case CMD_EXIT:
            return 1;

        default:
            printf("[DISPATCH] ERROR: unknown command id %d\n", msg->command);
            fflush(stdout);
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

    // ------------------------------------------------------------
    // 1) Validate arguments
    // ------------------------------------------------------------
    if (msg->arg1[0] == '\0' || msg->arg2[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    const char *username = msg->arg1;
    const char *permStr  = msg->arg2;

    // ------------------------------------------------------------
    // 2) Validate username
    // ------------------------------------------------------------
    {
        size_t len = strlen(username);
        if (len == 0 || len > 32 || username[0] == '-' ||
            strcmp(username, "root") == 0) {
            sendErrorMsg(clientFd);
            return 0;
        }

        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)username[i];
            if (!(isalnum(c) || c == '_' || c == '-')) {
                sendErrorMsg(clientFd);
                return 0;
            }
        }
    }

    // ------------------------------------------------------------
    // 3) Validate permissions (octal 0–777)
    // ------------------------------------------------------------
    char *endptr = NULL;
    long perms = strtol(permStr, &endptr, 8);

    if (endptr == permStr || *endptr != '\0' ||
        perms < 0 || perms > 0777) {
        sendErrorMsg(clientFd);
        return 0;
    }

    int permissions = (int)perms;

    // ------------------------------------------------------------
    // 4) Compute virtual home path
    // ------------------------------------------------------------
    char homePath[PATH_SIZE];
    snprintf(homePath, sizeof(homePath), "%s/%s", gRootDir, username);

    // ------------------------------------------------------------
    // 5) Elevate privileges
    // ------------------------------------------------------------
    uid_t old_euid;
    if (elevateToRoot(&old_euid) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    int error = 0;
    int userCreated = 0;   // system user created in THIS call
    int homeCreated = 0;   // virtual home created in THIS call

    // ------------------------------------------------------------
    // 6) Hard checks (NO auto cleanup)
    // ------------------------------------------------------------
    if (getpwnam(username) != NULL) {
        error = 1;
    }

    if (!error && fileExists(homePath)) {
        error = 1;
    }

    // ------------------------------------------------------------
    // 7) Create system user (NO /home)
    // ------------------------------------------------------------
    if (!error) {
        pid_t pid = fork();
        if (pid == 0) {
            execlp("adduser", "adduser",
                   "--disabled-password",
                   "--gecos", "",
                   "--ingroup", "csapgroup",
                   "--no-create-home",
                   username,
                   NULL);
            _exit(127);
        } else if (pid < 0) {
            error = 1;
        } else {
            int status;
            waitpid(pid, &status, 0);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                error = 1;
            } else {
                userCreated = 1;
            }
        }
    }

    // ------------------------------------------------------------
    // 8) Create virtual home directory
    // ------------------------------------------------------------
    if (!error) {
        if (mkdir(homePath, (mode_t)permissions) < 0) {
            error = 1;
        } else {
            homeCreated = 1;
        }
    }

    // ------------------------------------------------------------
    // 9) Set ownership and permissions
    // ------------------------------------------------------------
    if (!error) {
        struct passwd *pwd = getpwnam(username);
        struct group  *grp = getgrnam("csapgroup");

        if (!pwd || !grp ||
            chown(homePath, pwd->pw_uid, grp->gr_gid) < 0 ||
            chmod(homePath, (mode_t)permissions) < 0) {
            error = 1;
        }
    }

    // ------------------------------------------------------------
    // 10) Rollback (ONLY what was created here)
    // ------------------------------------------------------------
    if (error) {
        if (homeCreated && fileExists(homePath)) {
            removeRecursive(homePath);
        }

        if (userCreated) {
            pid_t pid = fork();
            if (pid == 0) {
                execlp("userdel", "userdel", username, NULL);
                _exit(127);
            } else if (pid > 0) {
                waitpid(pid, NULL, 0);
            }
        }
    }

    // ------------------------------------------------------------
    // 11) Drop privileges
    // ------------------------------------------------------------
    dropFromRoot(old_euid);

    if (error) {
        sendErrorMsg(clientFd);
        return 0;
    }

    printf("[CREATE_USER] OK '%s' perms=%o\n", username, permissions);
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

    // PROMJENA OVDJE: isInsideHome umjesto isInsideRoot
    if (!isInsideHome(session->homeDir, fullPath) || fileExists(fullPath)) {
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
// CHMOD - SA DEBUG ISPISIMA
// ================================================================
int handleChmod(int clientFd, ProtocolMessage *msg, Session *session)
{
    debugCommand("CHMOD", msg, session);

    // 1) Mora biti logovan
    if (!ensureLoggedIn(clientFd, session, "CHMOD")) {
        printf("[CHMOD DEBUG] Not logged in\n");
        return 0;
    }

    // 2) Argumenti moraju postojati
    if (msg->arg1[0] == '\0' || msg->arg2[0] == '\0') {
        printf("[CHMOD DEBUG] Missing arguments\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    // 3) Validacija permission-a (0–777 oktalno)
    char *endptr = NULL;
    long perms = strtol(msg->arg2, &endptr, 8);

    if (endptr == msg->arg2 || *endptr != '\0' ||
        perms < 0 || perms > 0777) {
        printf("[CHMOD DEBUG] Invalid permissions: %s\n", msg->arg2);
        sendErrorMsg(clientFd);
        return 0;
    }

    int permissions = (int)perms;
    printf("[CHMOD DEBUG] Permissions parsed: %o\n", permissions);

    // 4) Resolve path
    char fullPath[PATH_SIZE];
    if (resolvePath(session, msg->arg1, fullPath) < 0) {
        printf("[CHMOD DEBUG] resolvePath failed for: %s\n", msg->arg1);
        sendErrorMsg(clientFd);
        return 0;
    }
    
    printf("[CHMOD DEBUG] Resolved path: %s\n", fullPath);
    printf("[CHMOD DEBUG] Home dir: %s\n", session->homeDir);

    // 5) Mora biti unutar home direktorija
    if (!isInsideHome(session->homeDir, fullPath)) {
        printf("[CHMOD DEBUG] Not inside home: %s\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }
    
    printf("[CHMOD DEBUG] Inside home: OK\n");

    // 6) Mora postojati
    if (!fileExists(fullPath)) {
        printf("[CHMOD DEBUG] File doesn't exist: %s\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }
    
    printf("[CHMOD DEBUG] File exists: OK\n");

    // 7) Zaštita: samo root servera ne može mijenjati
    if (strcmp(fullPath, gRootDir) == 0) {
        printf("[CHMOD DEBUG] Cannot modify server root\n");
        sendErrorMsg(clientFd);
        return 0;
    }
    
    printf("[CHMOD DEBUG] Not server root: OK\n");

    // 8) Zaključaj target
    if (acquireFileLock(fullPath) < 0) {
        printf("[CHMOD DEBUG] acquireFileLock failed for: %s\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }
    
    printf("[CHMOD DEBUG] File locked: OK\n");

    // 9) Izvrši chmod (bez root privilegija)
    printf("[CHMOD DEBUG] Calling fsChmod(%s, %o)\n", fullPath, permissions);
    int rc = fsChmod(fullPath, permissions);
    printf("[CHMOD DEBUG] fsChmod returned: %d\n", rc);

    // 10) Oslobodi lock
    releaseFileLock(fullPath);
    printf("[CHMOD DEBUG] File unlocked\n");

    // 11) Provjeri rezultat
    if (rc < 0) {
        printf("[CHMOD DEBUG] fsChmod failed\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    // 12) Sve OK
    printf("[CHMOD DEBUG] Success! Sending OK to client\n");
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

    // PROMJENA OVDJE: isInsideHome umjesto isInsideRoot
    if (!isInsideHome(session->homeDir, src) ||
        !isInsideHome(session->homeDir, dst) ||
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
    if (resolvePath(session, msg->arg1, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // PROMJENA OVDJE: isInsideHome umjesto isInsideRoot
    if (!isInsideHome(session->homeDir, fullPath)) {
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
// ================================================================
// LIST - POPRAVLJENA VERZIJA
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
    // LIST JE JEDINA KOJA MOŽE IZAĆI IZ HOME, ALI NE IZ ROOT-A
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

    mode_t mode = st.st_mode;

    if (!S_ISDIR(st.st_mode)) {
        printf("[LIST] ERROR: Not a directory: '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // ============================================
    // 4) PROVJERA PERMISIJA - KLJUČNA POPRAVKA!
    // ============================================
    
    // Ako je direktorij unutar MOG home-a, gledaj OWNER permisije
    if (isInsideHome(session->homeDir, fullPath)) {
        // Ja sam vlasnik (jer je u mom home-u), gledaj owner permisije
        if (!(mode & S_IRUSR) || !(mode & S_IXUSR)) {
            printf("[LIST] PERMISSION DENIED (owner) for '%s'\n", fullPath);
            sendErrorMsg(clientFd);
            return 0;
        }
    }
    else {
        // Direktorij je izvan mog home-a (tudji home), gledaj GROUP permisije
        // Jer svi korisnici su u istoj grupi (csapgroup)
        if (!(mode & S_IRGRP) || !(mode & S_IXGRP)) {
            printf("[LIST] PERMISSION DENIED (group) for '%s'\n", fullPath);
            sendErrorMsg(clientFd);
            return 0;
        }
    }

    // ============================================
    // 5) OTVORI DIREKTORIJ
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
        size_t fp = strlen(fullPath);
        size_t en = strlen(entry->d_name);

        if (fp + 1 + en + 1 >= PATH_SIZE)
            continue; // preskoči preduga imena

        memcpy(entryPath, fullPath, fp);
        entryPath[fp] = '/';
        memcpy(entryPath + fp + 1, entry->d_name, en);
        entryPath[fp + 1 + en] = '\0';

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

    if (resolvePath(session, msg->arg1, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // PROMJENA OVDJE: isInsideHome umjesto isInsideRoot
    if (!isInsideHome(session->homeDir, fullPath) ||
        !fileExists(fullPath)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    int offset = 0;
    if (msg->arg2[0] != '\0') {
        offset = atoi(msg->arg2);
        if (offset < 0) offset = 0;
    }

    // ⬇⬇⬇ LOCK PRIJE STAT ⬇⬇⬇
    if (acquireFileLock(fullPath) < 0) {
        printf("[READ] file in use '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    struct stat st;
    if (stat(fullPath, &st) < 0 || !S_ISREG(st.st_mode)) {
        releaseFileLock(fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    int fileSize = (int)st.st_size;
    if (offset > fileSize)
        offset = fileSize;

    int toRead = fileSize - offset;

    char *buffer = NULL;
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

    printf("[READ] %d bytes from '%s' (offset=%d)\n",
           readBytes, fullPath, offset);
    return 0;
}

// ================================================================
// WRITE  (PDF: write <path>, write -offset=N <path>)
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

    if (resolvePath(session, msg->arg1, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // PROMJENA OVDJE: isInsideHome umjesto isInsideRoot
    if (!isInsideHome(session->homeDir, fullPath)) {
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

    // 1) ACK klijentu
    sendOk(clientFd, 0);

    // 2) primi size
    int size = 0;
    if (recvAll(clientFd, &size, sizeof(int)) < 0 || size < 0) {
        releaseFileLock(fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    char *buffer = NULL;

    if (size > 0) {
        buffer = malloc(size);
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
    }

    // ⬇⬇⬇ KLJUČ: piši I kad je size == 0 ⬇⬇⬇
    int written = fsWriteFile(fullPath, buffer, size, offset);

    if (buffer)
        free(buffer);

    releaseFileLock(fullPath);

    if (written < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    sendOk(clientFd, written);
    printf("[WRITE] %d bytes -> '%s' (offset=%d)\n",
           written, fullPath, offset);
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

    if (resolvePath(session, msg->arg1, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // PROMJENA OVDJE: isInsideHome umjesto isInsideRoot
    if (!isInsideHome(session->homeDir, fullPath) ||
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

    if (!msg->arg1[0] || size < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    if (resolvePath(session, msg->arg1, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // PROMJENA OVDJE: isInsideHome umjesto isInsideRoot
    if (!isInsideHome(session->homeDir, fullPath)) {
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
        resolvePath(session, msg->arg1, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // PROMJENA OVDJE: isInsideHome umjesto isInsideRoot
    if (!isInsideHome(session->homeDir, fullPath)) {
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

    // 1) Mora biti odjavljen
    if (session->isLoggedIn) {
        printf("[DELETE_USER] ERROR: must NOT be logged in\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    // 2) Username obavezan
    if (msg->arg1[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 2.1) NE SMIJE BITI DODATNIH ARGUMENATA
    if (msg->arg2[0] != '\0' || msg->arg3[0] != '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    const char *target = msg->arg1;

    // 3) Zaštita
    if (strcmp(target, "root") == 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // ============================================================
    // 4) PROVJERI DA LI USER POSTOJI
    // ============================================================
    struct passwd *pwd = getpwnam(target);
    if (!pwd) {
        sendErrorMsg(clientFd);
        return 0;
    }

    char homePath[PATH_SIZE];
    snprintf(homePath, PATH_SIZE, "%s/%s", gRootDir, target);

    // 5) Privremeni root
    uid_t old_euid;
    if (elevateToRoot(&old_euid) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    int error = 0;

    // ============================================================
    // A) userdel -r <user>
    // ============================================================
    {
        pid_t pid = fork();
        if (pid < 0) {
            error = 1;
        } else if (pid == 0) {
            execlp("userdel", "userdel", "-r", target, NULL);
            _exit(127);
        } else {
            int status;
            waitpid(pid, &status, 0);

            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                error = 1;
            }
        }
    }

    // ============================================================
    // B) ukloni /var/mail/<user> ako postoji
    // ============================================================
    {
        char mailPath[PATH_SIZE];
        snprintf(mailPath, sizeof(mailPath), "/var/mail/%s", target);
        unlink(mailPath); // ignoriši grešku
    }

    // ============================================================
    // C) OBRIŠI VIRTUAL HOME U ROOTDIR
    // ============================================================
    if (fileExists(homePath)) {
        if (removeRecursive(homePath) < 0) {
            error = 1;
        }
    }

    // 6) Vrati privilegije
    dropFromRoot(old_euid);

    if (error) {
        sendErrorMsg(clientFd);
        return 0;
    }

    printf("[DELETE_USER] '%s' deleted successfully\n", target);
    sendOk(clientFd, 0);
    return 0;
}