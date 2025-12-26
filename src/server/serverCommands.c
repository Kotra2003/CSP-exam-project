#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>

#include "../../include/serverCommands.h"
#include "../../include/protocol.h"
#include "../../include/session.h"
#include "../../include/utils.h"
#include "../../include/fsOps.h"
#include "../../include/concurrency.h"
#include "../../include/network.h"

// Global server root directory
extern const char *gRootDir;

// ================================================================
// Debug helper: print real/effective UID and GID
// ================================================================
static inline void debugWhoAmI(const char *where)
{
    printf("[WHOAMI] %-20s | ruid=%d euid=%d rgid=%d egid=%d\n",
           where,
           getuid(), geteuid(),
           getgid(), getegid());
    fflush(stdout);
}

// ================================================================
// Helper: send simple response to client
// ================================================================
static void sendStatus(int clientFd, int status, int dataSize)
{
    ProtocolResponse res;
    res.status   = status;
    res.dataSize = dataSize;
    sendResponse(clientFd, &res);
}

// Send STATUS_OK
static void sendOk(int clientFd, int dataSize)
{
    sendStatus(clientFd, STATUS_OK, dataSize);
}

// Send STATUS_ERROR
static void sendErrorMsg(int clientFd)
{
    sendStatus(clientFd, STATUS_ERROR, 0);
}

// ================================================================
// Session / debug helpers
// ================================================================

// Ensure user is logged in before executing command
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

// Print debug information about received command
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

// Temporarily elevate effective UID to root
static int elevateToRoot(uid_t *old_euid)
{
    *old_euid = geteuid();

    if (seteuid(0) != 0) {
        perror("[PRIV] ERROR: seteuid(0) failed (server not started with sudo)");
        return -1;
    }
    return 0;
}

// Drop root privileges and restore previous effective UID
static void dropFromRoot(uid_t old_euid)
{
    if (seteuid(old_euid) != 0) {
        perror("[PRIV] ERROR: failed to drop root privileges");
    }
}

// ================================================================
// Switch THIS child process to the logged-in user identity
//  - set effective GID to "csapgroup"
//  - set supplementary groups (initgroups)
//  - set effective UID to user's UID
//  - process stays as user (root only via elevateToRoot when needed)
// ================================================================
static int becomeLoggedUser(const char *username)
{
    uid_t old_euid;

    // Temporarily become root to change identity
    if (elevateToRoot(&old_euid) < 0) {
        return -1;
    }

    struct passwd *pwd = getpwnam(username);
    struct group  *grp = getgrnam("csapgroup");

    if (!pwd || !grp) {
        dropFromRoot(old_euid);
        return -1;
    }

    // Set effective group ID
    if (setegid(grp->gr_gid) != 0) {
        perror("[LOGIN] setegid failed");
        dropFromRoot(old_euid);
        return -1;
    }

    // Initialize supplementary groups for user
    if (initgroups(username, grp->gr_gid) != 0) {
        perror("[LOGIN] initgroups failed");
        dropFromRoot(old_euid);
        return -1;
    }

    // Drop privileges to logged-in user
    if (seteuid(pwd->pw_uid) != 0) {
        perror("[LOGIN] seteuid(user) failed");
        dropFromRoot(old_euid);
        return -1;
    }

    /*
     * IMPORTANT:
     * We intentionally do NOT restore old_euid here.
     * From this point, the child process runs as the logged-in user.
     * Root privileges are re-acquired only via elevateToRoot().
     */
    return 0;
}

// ================================================================
// COMMAND DISPATCHER
// Routes protocol command IDs to handler functions
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
            // Signal server loop to close connection
            return 1;

        default:
            // Unknown command
            printf("[DISPATCH] ERROR: unknown command id %d\n", msg->command);
            fflush(stdout);
            sendErrorMsg(clientFd);
            return 0;
    }
}

// ================================================================
// LOGIN HANDLER
// Authenticates user and switches process identity
// ================================================================
int handleLogin(int clientFd, ProtocolMessage *msg, Session *session)
{
    // Debug: print command and session state
    debugCommand("LOGIN", msg, session);

    // Prevent double login
    if (session->isLoggedIn) {
        printf("[LOGIN] ERROR: already logged in.\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    // Username must be provided
    if (msg->arg1[0] == '\0') {
        printf("[LOGIN] ERROR: missing username.\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    // Build expected home directory path
    char homePath[PATH_SIZE];
    snprintf(homePath, PATH_SIZE, "%s/%s", gRootDir, msg->arg1);

    // ------------------------------------------------------------
    // Check user directory existence (requires root)
    // ------------------------------------------------------------
    uid_t old_euid;
    if (elevateToRoot(&old_euid) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    int exists = fileExists(homePath);

    // Drop root privileges immediately
    dropFromRoot(old_euid);

    if (!exists) {
        printf("[LOGIN] ERROR: no such user dir '%s'\n", homePath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // Initialize session (username, homeDir, currentDir)
    loginUser(session, gRootDir, msg->arg1);

    // ------------------------------------------------------------
    // Switch THIS child process to the logged-in user
    // ------------------------------------------------------------
    if (becomeLoggedUser(session->username) < 0) {
        printf("[LOGIN] ERROR: cannot switch to user '%s'\n", session->username);

        // Roll back session state
        session->isLoggedIn    = 0;
        session->username[0]   = '\0';
        session->homeDir[0]    = '\0';
        session->currentDir[0] = '\0';

        sendErrorMsg(clientFd);
        return 0;
    }

    printf("[LOGIN] OK user='%s' (euid=%d egid=%d)\n",
           session->username, (int)geteuid(), (int)getegid());

    sendOk(clientFd, 0);
    return 0;
}

// ================================================================
// CREATE USER
// Creates a new user directory (requires root privileges)
// ================================================================
int handleCreateUser(int clientFd, ProtocolMessage *msg, Session *session)
{
    // Debug: print command info
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
    // 2) Validate username format
    // ------------------------------------------------------------
    {
        size_t len = strlen(username);

        // Basic sanity checks
        if (len == 0 || len > 32 || username[0] == '-' ||
            strcmp(username, "root") == 0) {
            sendErrorMsg(clientFd);
            return 0;
        }

        // Allow only [a-zA-Z0-9_-]
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

    // Must be valid octal and in range
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
    // 5) Elevate privileges (root required)
    // ------------------------------------------------------------
    uid_t old_euid;
    if (elevateToRoot(&old_euid) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    int error = 0;
    int userCreated = 0;   // System user created in this call
    int homeCreated = 0;   // Home directory created in this call

    // ------------------------------------------------------------
    // 6) Hard checks (no auto-cleanup here)
    // ------------------------------------------------------------
    if (getpwnam(username) != NULL) {
        error = 1; // System user already exists
    }

    if (!error && fileExists(homePath)) {
        error = 1; // Virtual home already exists
    }

    // ------------------------------------------------------------
    // 7) Create system user (without /home)
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
    // 10) Rollback (only what was created here)
    // ------------------------------------------------------------
    if (error) {
        // Remove virtual home if created
        if (homeCreated && fileExists(homePath)) {
            removeRecursive(homePath);
        }

        // Remove system user if created
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
    // 11) Drop root privileges
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
// CREATE FILE OR DIRECTORY
// ================================================================
int handleCreate(int clientFd, ProtocolMessage *msg, Session *session)
{
    // Optional debug: print real/effective UID/GID
    //debugWhoAmI("CREATE");

    // Print command info for debugging
    debugCommand("CREATE", msg, session);

    // 1) User must be logged in
    if (!ensureLoggedIn(clientFd, session, "CREATE"))
        return 0;

    const char *pathArg = msg->arg1;
    const char *permArg = msg->arg2;
    const char *typeArg = msg->arg3;

    // 2) Basic argument validation
    if (!pathArg[0] || !permArg[0]) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // ============================================
    // PERMISSION VALIDATION (octal 0–777)
    // ============================================
    char *endptr;
    long perms = strtol(permArg, &endptr, 8);

    // Must be valid octal value
    if (*endptr != '\0' || perms < 0 || perms > 0777) {
        printf("[CREATE] invalid permissions: %s\n", permArg);
        sendErrorMsg(clientFd);
        return 0;
    }

    int permissions = (int)perms;

    // ============================================
    // CHECK IF DIRECTORY FLAG IS SET
    // ============================================
    // "-d" means directory, otherwise create file
    int isDir = (strcmp(typeArg, "-d") == 0);

    // 3) Resolve absolute filesystem path
    char fullPath[PATH_SIZE];
    if (resolvePath(session, pathArg, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 4) Security checks:
    //    - must be inside user's home directory
    //    - target must not already exist
    if (!isInsideHome(session->homeDir, fullPath) || fileExists(fullPath)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 5) Create file or directory
    if (fsCreate(fullPath, permissions, isDir) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 6) Success
    sendOk(clientFd, 0);
    return 0;
}
// ================================================================
// CHMOD
// ================================================================
int handleChmod(int clientFd, ProtocolMessage *msg, Session *session)
{
    // Optional debug: print UID/GID
    //debugWhoAmI("CHMOD");

    // Print command data for debugging
    debugCommand("CHMOD", msg, session);

    // 1) User must be logged in
    if (!ensureLoggedIn(clientFd, session, "CHMOD")) {
        printf("[CHMOD DEBUG] Not logged in\n");
        return 0;
    }

    // 2) Arguments must exist
    if (msg->arg1[0] == '\0' || msg->arg2[0] == '\0') {
        printf("[CHMOD DEBUG] Missing arguments\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    // 3) Validate permissions (octal 0–777)
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

    // 4) Resolve full path
    char fullPath[PATH_SIZE];
    if (resolvePath(session, msg->arg1, fullPath) < 0) {
        printf("[CHMOD DEBUG] resolvePath failed for: %s\n", msg->arg1);
        sendErrorMsg(clientFd);
        return 0;
    }

    printf("[CHMOD DEBUG] Resolved path: %s\n", fullPath);
    printf("[CHMOD DEBUG] Home dir: %s\n", session->homeDir);

    // 5) Path must be inside user's home directory
    if (!isInsideHome(session->homeDir, fullPath)) {
        printf("[CHMOD DEBUG] Not inside home: %s\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    printf("[CHMOD DEBUG] Inside home: OK\n");

    // 6) Target must exist
    if (!fileExists(fullPath)) {
        printf("[CHMOD DEBUG] File doesn't exist: %s\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    printf("[CHMOD DEBUG] File exists: OK\n");

    // 7) Protect server root directory
    if (strcmp(fullPath, gRootDir) == 0) {
        printf("[CHMOD DEBUG] Cannot modify server root\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    printf("[CHMOD DEBUG] Not server root: OK\n");

    // 8) Acquire exclusive lock on target
    if (acquireFileLock(fullPath) < 0) {
        printf("[CHMOD DEBUG] acquireFileLock failed for: %s\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    printf("[CHMOD DEBUG] File locked: OK\n");

    // 9) Perform chmod as logged-in user (no root)
    printf("[CHMOD DEBUG] Calling fsChmod(%s, %o)\n", fullPath, permissions);
    int rc = fsChmod(fullPath, permissions);
    printf("[CHMOD DEBUG] fsChmod returned: %d\n", rc);

    // 10) Release lock
    releaseFileLock(fullPath);
    printf("[CHMOD DEBUG] File unlocked\n");

    // 11) Check result
    if (rc < 0) {
        printf("[CHMOD DEBUG] fsChmod failed\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    // 12) Success
    printf("[CHMOD DEBUG] Success! Sending OK to client\n");
    sendOk(clientFd, 0);
    return 0;
}


// ================================================================
// MOVE (rename / move file or directory)
// ================================================================
int handleMove(int clientFd, ProtocolMessage *msg, Session *session)
{
    // Optional UID/GID debug
    //debugWhoAmI("MOVE");

    // Print command details for debugging
    debugCommand("MOVE", msg, session);

    // 1) User must be logged in
    if (!ensureLoggedIn(clientFd, session, "MOVE"))
        return 0;

    char src[PATH_SIZE], dst[PATH_SIZE];

    // 2) Both source and destination arguments must exist
    if (!msg->arg1[0] || !msg->arg2[0]) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 3) Resolve absolute paths
    if (resolvePath(session, msg->arg1, src) < 0 ||
        resolvePath(session, msg->arg2, dst) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 4) Security checks:
    //    - both paths must be inside user's home
    //    - source must exist
    //    - destination must NOT exist
    if (!isInsideHome(session->homeDir, src) ||
        !isInsideHome(session->homeDir, dst) ||
        !fileExists(src) ||
        fileExists(dst)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 5) Acquire locks on both paths (prevent race conditions)
    if (acquireFileLock(src) < 0 || acquireFileLock(dst) < 0) {
        releaseFileLock(src);
        releaseFileLock(dst);
        sendErrorMsg(clientFd);
        return 0;
    }

    // 6) Perform move / rename
    int ok = fsMove(src, dst);

    // 7) Release locks
    releaseFileLock(src);
    releaseFileLock(dst);

    // 8) Check result
    if (ok < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 9) Success
    sendOk(clientFd, 0);
    return 0;
}


// ================================================================
// CD (change directory)
// ================================================================
int handleCd(int clientFd, ProtocolMessage *msg, Session *session)
{
    // Optional UID/GID debug
    //debugWhoAmI("CD");

    // Print command details for debugging
    debugCommand("CD", msg, session);

    // 1) User must be logged in
    if (!ensureLoggedIn(clientFd, session, "CD"))
        return 0;

    char fullPath[PATH_SIZE];

    // 2) No argument: go to home directory
    if (msg->arg1[0] == '\0') {
        // Update session current directory to home
        strncpy(session->currentDir, session->homeDir, PATH_SIZE);

        // Send "/" as display path
        char displayPath[PATH_SIZE] = "/";
        sendOk(clientFd, strlen(displayPath));
        if (strlen(displayPath) > 0) {
            sendAll(clientFd, displayPath, strlen(displayPath));
        }
        return 0;
    }

    // 3) Resolve target path
    if (resolvePath(session, msg->arg1, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 4) Target must be inside user's home directory
    if (!isInsideHome(session->homeDir, fullPath)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 5) Target must exist and be a directory
    struct stat st;
    if (stat(fullPath, &st) < 0 || !S_ISDIR(st.st_mode)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 6) Update session current directory
    strncpy(session->currentDir, fullPath, PATH_SIZE);

    // 7) Build display path relative to home directory
    char displayPath[PATH_SIZE];
    size_t homeLen = strlen(session->homeDir);

    if (strncmp(session->currentDir, session->homeDir, homeLen) == 0) {
        if (session->currentDir[homeLen] == '\0') {
            strcpy(displayPath, "/");
        } else if (session->currentDir[homeLen] == '/') {
            snprintf(displayPath, PATH_SIZE, "/%s",
                     session->currentDir + homeLen + 1);
        } else {
            strcpy(displayPath, "/");
        }
    } else {
        strcpy(displayPath, "/");
    }

    // 8) Send OK with new display path
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
// FIXED / IMPROVED LIST IMPLEMENTATION
// ================================================================
int handleList(int clientFd, ProtocolMessage *msg, Session *session)
{
    // Optional UID/GID debug
    //debugWhoAmI("LIST");

    // Print command info for debugging
    debugCommand("LIST", msg, session);

    // 1) User must be logged in
    if (!ensureLoggedIn(clientFd, session, "LIST"))
        return 0;

    char fullPath[PATH_SIZE];
    char output[8192];
    output[0] = '\0';

    // ============================================
    // 1) Determine directory to list
    // ============================================
    if (msg->arg1[0] == '\0') {
        // No argument -> current directory
        strncpy(fullPath, session->currentDir, PATH_SIZE);
        fullPath[PATH_SIZE - 1] = '\0';
    }
    else if (msg->arg1[0] == '/') {
        // Absolute path inside server root
        // Example: /admin       -> <rootDir>/admin
        //          /admin/docs  -> <rootDir>/admin/docs
        snprintf(fullPath, PATH_SIZE, "%s/%s", gRootDir, msg->arg1 + 1);
    }
    else {
        // Relative path -> resolve against current directory
        if (resolvePath(session, msg->arg1, fullPath) < 0) {
            printf("[LIST] ERROR: resolvePath failed for '%s'\n", msg->arg1);
            sendErrorMsg(clientFd);
            return 0;
        }
    }

    // ============================================
    // 2) Security check: must stay inside server root
    // LIST is the only command allowed outside home,
    // but NEVER outside the server root directory
    // ============================================
    if (!isInsideRoot(gRootDir, fullPath)) {
        printf("[LIST] ERROR: Path outside root: '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // ============================================
    // 3) Check if directory exists
    // ============================================
    struct stat st;
    if (stat(fullPath, &st) < 0) {
        printf("[LIST] ERROR: stat failed for '%s': %s\n",
               fullPath, strerror(errno));
        sendErrorMsg(clientFd);
        return 0;
    }

    if (!S_ISDIR(st.st_mode)) {
        printf("[LIST] ERROR: Not a directory: '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    mode_t mode = st.st_mode;

    // ============================================
    // 4) PERMISSION CHECK (KEY FIX)
    // ============================================
    // If directory is inside user's own home,
    // check OWNER permissions (r + x)
    if (isInsideHome(session->homeDir, fullPath)) {
        if (!(mode & S_IRUSR) || !(mode & S_IXUSR)) {
            printf("[LIST] PERMISSION DENIED (owner) for '%s'\n", fullPath);
            sendErrorMsg(clientFd);
            return 0;
        }
    }
    else {
        // Directory is outside user's home (other users)
        // All users belong to the same group (csapgroup),
        // so check GROUP permissions (r + x)
        if (!(mode & S_IRGRP) || !(mode & S_IXGRP)) {
            printf("[LIST] PERMISSION DENIED (group) for '%s'\n", fullPath);
            sendErrorMsg(clientFd);
            return 0;
        }
    }

    // ============================================
    // 5) Open directory
    // ============================================
    DIR *dir = opendir(fullPath);
    if (!dir) {
        printf("[LIST] ERROR: opendir failed for '%s': %s\n",
               fullPath, strerror(errno));
        sendErrorMsg(clientFd);
        return 0;
    }

    // Output header
    strcat(output, "============================================================\n");
    strcat(output, "                         CONTENTS                          \n");
    strcat(output, "------------------------------------------------------------\n");
    strcat(output, " NAME                              PERMISSIONS     SIZE     \n");
    strcat(output, "------------------------------------------------------------\n");

    struct dirent *entry;
    int itemCount = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        // Skip ".", ".." and internal ".lock" files
        if (!strcmp(entry->d_name, ".") ||
            !strcmp(entry->d_name, "..") ||
            strstr(entry->d_name, ".lock") != NULL)
            continue;

        char entryPath[PATH_SIZE];
        size_t fp = strlen(fullPath);
        size_t en = strlen(entry->d_name);

        // Skip too-long paths
        if (fp + 1 + en + 1 >= PATH_SIZE)
            continue;

        memcpy(entryPath, fullPath, fp);
        entryPath[fp] = '/';
        memcpy(entryPath + fp + 1, entry->d_name, en);
        entryPath[fp + 1 + en] = '\0';

        // Stat entry
        if (stat(entryPath, &st) < 0) {
            continue;
        }

        int  perms = st.st_mode & 0777;
        long size  = (long)st.st_size;
        int  isDir = S_ISDIR(st.st_mode);

        // Format output line
        char line[512];
        if (isDir) {
            snprintf(line, sizeof(line),
                     " %-30s [DIR]  %04o      %6ld\n",
                     entry->d_name, perms, size);
        } else {
            snprintf(line, sizeof(line),
                     " %-30s [FILE] %04o      %6ld\n",
                     entry->d_name, perms, size);
        }

        strncat(output, line,
                sizeof(output) - strlen(output) - 1);
        itemCount++;
    }

    closedir(dir);

    // Output footer
    strcat(output, "------------------------------------------------------------\n");

    char footer[128];
    snprintf(footer, sizeof(footer),
             " Total: %d item(s)\n", itemCount);
    strcat(output, footer);

    strcat(output, "============================================================\n");

    // Debug: print size of response
    printf("[LIST] OK: Sending %ld bytes for path '%s'\n",
           strlen(output), fullPath);

    // Send response
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
    // Optional UID/GID debug
    //debugWhoAmI("READ");

    // Print command details for debugging
    debugCommand("READ", msg, session);

    // 1) User must be logged in
    if (!ensureLoggedIn(clientFd, session, "READ"))
        return 0;

    // 2) Path argument must exist
    if (msg->arg1[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    char fullPath[PATH_SIZE];

    // 3) Resolve absolute path
    if (resolvePath(session, msg->arg1, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 4) Security checks:
    //    - file must be inside user's home directory
    //    - file must exist
    if (!isInsideHome(session->homeDir, fullPath) ||
        !fileExists(fullPath)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 5) Parse optional offset
    int offset = 0;
    if (msg->arg2[0] != '\0') {
        offset = atoi(msg->arg2);
        if (offset < 0) offset = 0;
    }

    // ⬇⬇⬇ ACQUIRE LOCK BEFORE STAT (race-condition safe) ⬇⬇⬇
    if (acquireFileLock(fullPath) < 0) {
        printf("[READ] file in use '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // 6) Check file type and size
    struct stat st;
    if (stat(fullPath, &st) < 0 || !S_ISREG(st.st_mode)) {
        releaseFileLock(fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    int fileSize = (int)st.st_size;

    // Clamp offset to file size
    if (offset > fileSize)
        offset = fileSize;

    int toRead = fileSize - offset;

    char *buffer = NULL;
    int   readBytes = 0;

    // 7) Read file content (if any)
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

    // 8) Release file lock
    releaseFileLock(fullPath);

    // 9) Send OK with number of bytes read
    sendOk(clientFd, readBytes);

    // 10) Send file data
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
    // Optional UID/GID debug
    //debugWhoAmI("WRITE");

    // Print command details for debugging
    debugCommand("WRITE", msg, session);

    // 1) User must be logged in
    if (!ensureLoggedIn(clientFd, session, "WRITE"))
        return 0;

    // 2) Path argument must exist
    if (msg->arg1[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    char fullPath[PATH_SIZE];

    // 3) Resolve absolute path
    if (resolvePath(session, msg->arg1, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 4) Target must be inside user's home directory
    if (!isInsideHome(session->homeDir, fullPath)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 5) Parse optional offset
    int offset = 0;
    if (msg->arg2[0] != '\0') {
        offset = atoi(msg->arg2);
        if (offset < 0) offset = 0;
    }

    // 6) Acquire exclusive lock on target
    if (acquireFileLock(fullPath) < 0) {
        printf("[WRITE] file in use '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // 7) Send ACK to client (ready to receive data)
    sendOk(clientFd, 0);

    // 8) Receive data size
    int size = 0;
    if (recvAll(clientFd, &size, sizeof(int)) < 0 || size < 0) {
        releaseFileLock(fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    char *buffer = NULL;

    // 9) Receive data buffer (if size > 0)
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

    // ⬇⬇⬇ KEY POINT: write even when size == 0 (truncate support) ⬇⬇⬇
    int written = fsWriteFile(fullPath, buffer, size, offset);

    if (buffer)
        free(buffer);

    // 10) Release lock
    releaseFileLock(fullPath);

    // 11) Check result
    if (written < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 12) Send final OK with number of bytes written
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
    // Optional UID/GID debug
    //debugWhoAmI("DELETE");

    // Print command details for debugging
    debugCommand("DELETE", msg, session);

    // 1) User must be logged in
    if (!ensureLoggedIn(clientFd, session, "DELETE"))
        return 0;

    // 2) Path argument must exist
    if (msg->arg1[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    char fullPath[PATH_SIZE];

    // 3) Resolve absolute path
    if (resolvePath(session, msg->arg1, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 4) Target must be inside user's home directory and must exist
    if (!isInsideHome(session->homeDir, fullPath) ||
        !fileExists(fullPath)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // ----------------------------------------
    // 5) Acquire lock (prevents deleting in-use files)
    // ----------------------------------------
    if (acquireFileLock(fullPath) < 0) {
        printf("[DELETE] file in use: %s\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // ----------------------------------------
    // 6) Remove associated ".lock" file if it exists
    // ----------------------------------------
    char lockPath[PATH_SIZE + 10];
    snprintf(lockPath, sizeof(lockPath), "%s.lock", fullPath);
    unlink(lockPath);  // ignore errors

    // ----------------------------------------
    // 7) Delete file or directory recursively
    // ----------------------------------------
    int ok = removeRecursive(fullPath);

    // 8) Release lock
    releaseFileLock(fullPath);

    // 9) Check result
    if (ok < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 10) Success
    sendOk(clientFd, 0);
    return 0;
}

// ================================================================
// UPLOAD
// ================================================================
int handleUpload(int clientFd, ProtocolMessage *msg, Session *session)
{
    // Optional UID/GID debug
    //debugWhoAmI("UPLOAD");

    // Print command details for debugging
    debugCommand("UPLOAD", msg, session);

    // 1) User must be logged in
    if (!ensureLoggedIn(clientFd, session, "UPLOAD"))
        return 0;

    char fullPath[PATH_SIZE];
    int size = atoi(msg->arg2);

    // 2) Validate arguments
    if (!msg->arg1[0] || size < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 3) Resolve absolute path
    if (resolvePath(session, msg->arg1, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 4) Target must be inside user's home directory
    if (!isInsideHome(session->homeDir, fullPath)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 5) Acquire exclusive lock on target file
    if (acquireFileLock(fullPath) < 0) {
        printf("[UPLOAD] file in use '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // 6) Acknowledge client and request file data
    sendOk(clientFd, 0);

    // 7) Receive file content
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

    // 8) Write received data to file (overwrite)
    int written = fsWriteFile(fullPath, buffer, size, 0);

    free(buffer);
    releaseFileLock(fullPath);

    // 9) Check result
    if (written < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 10) Send final OK with number of bytes written
    sendOk(clientFd, written);
    return 0;
}

// ================================================================
// DOWNLOAD
// ================================================================
int handleDownload(int clientFd, ProtocolMessage *msg, Session *session)
{
    // Optional UID/GID debug
    //debugWhoAmI("DOWNLOAD");

    // Print command details for debugging
    debugCommand("DOWNLOAD", msg, session);

    // 1) User must be logged in
    if (!ensureLoggedIn(clientFd, session, "DOWNLOAD"))
        return 0;

    char fullPath[PATH_SIZE];

    // 2) Validate and resolve path
    if (!msg->arg1[0] ||
        resolvePath(session, msg->arg1, fullPath) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 3) Target must be inside user's home directory
    if (!isInsideHome(session->homeDir, fullPath)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 4) Target must be a regular file
    struct stat st;
    if (stat(fullPath, &st) < 0 || !S_ISREG(st.st_mode)) {
        sendErrorMsg(clientFd);
        return 0;
    }

    int size = (int)st.st_size;

    // 5) Acquire exclusive lock on file
    if (acquireFileLock(fullPath) < 0) {
        printf("[DOWNLOAD] file in use '%s'\n", fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    // 6) Allocate buffer and read file
    char *buffer = malloc(size);
    if (!buffer) {
        releaseFileLock(fullPath);
        sendErrorMsg(clientFd);
        return 0;
    }

    int readBytes = fsReadFile(fullPath, buffer, size, 0);

    // 7) Release lock
    releaseFileLock(fullPath);

    // 8) Check read result
    if (readBytes < 0) {
        free(buffer);
        sendErrorMsg(clientFd);
        return 0;
    }

    // 9) Send file size and content to client
    sendOk(clientFd, readBytes);
    sendAll(clientFd, buffer, readBytes);

    free(buffer);
    return 0;
}

// ================================================================
// DELETE USER (with temporary root privileges)
// ================================================================
int handleDeleteUser(int clientFd, ProtocolMessage *msg, Session *session)
{
    // Optional UID/GID debug
    //debugWhoAmI("DELETE_USER (entry)");

    // Print command details for debugging
    debugCommand("DELETE_USER", msg, session);

    // 1) User must NOT be logged in
    if (session->isLoggedIn) {
        printf("[DELETE_USER] ERROR: must NOT be logged in\n");
        sendErrorMsg(clientFd);
        return 0;
    }

    // 2) Username is required
    if (msg->arg1[0] == '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    // 2.1) No extra arguments allowed
    if (msg->arg2[0] != '\0' || msg->arg3[0] != '\0') {
        sendErrorMsg(clientFd);
        return 0;
    }

    const char *target = msg->arg1;

    // 3) Protection: root user cannot be deleted
    if (strcmp(target, "root") == 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // ============================================================
    // 4) Check if system user exists
    // ============================================================
    struct passwd *pwd = getpwnam(target);
    if (!pwd) {
        sendErrorMsg(clientFd);
        return 0;
    }

    char homePath[PATH_SIZE];
    snprintf(homePath, PATH_SIZE, "%s/%s", gRootDir, target);

    // Optional debug before privilege escalation
    //debugWhoAmI("DELETE_USER before root");

    // 5) Temporarily elevate privileges to root
    uid_t old_euid;
    if (elevateToRoot(&old_euid) < 0) {
        sendErrorMsg(clientFd);
        return 0;
    }

    // Optional debug after privilege escalation
    //debugWhoAmI("DELETE_USER after root");

    int error = 0;

    // ============================================================
    // A) Delete system user (userdel -r <user>)
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
    // B) Remove /var/mail/<user> if it exists
    // ============================================================
    {
        char mailPath[PATH_SIZE];
        snprintf(mailPath, sizeof(mailPath), "/var/mail/%s", target);
        unlink(mailPath);  // ignore errors
    }

    // ============================================================
    // C) Remove virtual home directory inside server root
    // ============================================================
    if (fileExists(homePath)) {
        if (removeRecursive(homePath) < 0) {
            error = 1;
        }
    }

    // 6) Drop root privileges
    dropFromRoot(old_euid);

    // Optional debug after dropping privileges
    //debugWhoAmI("DELETE_USER after drop");

    // 7) Final result
    if (error) {
        sendErrorMsg(clientFd);
        return 0;
    }

    printf("[DELETE_USER] '%s' deleted successfully\n", target);
    sendOk(clientFd, 0);
    return 0;
}
