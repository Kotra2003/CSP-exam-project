#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <sys/file.h>

#include "../../include/fsOps.h"
#include "../../include/utils.h"

// Global root directory
extern const char *gRootDir;

// ============================================================
// LOCKING — fcntl()
// ============================================================

// Acquire shared (read) lock on entire file
int lockFileRead(int fd)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_RDLCK;      // Shared lock
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;        // Lock whole file

    // Blocking call until lock is acquired
    return fcntl(fd, F_SETLKW, &fl);
}

// Acquire exclusive (write) lock on entire file
int lockFileWrite(int fd)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_WRLCK;          // Exclusive lock
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;

    // Blocking call until lock is acquired
    return fcntl(fd, F_SETLKW, &fl);
}

// Release any lock held on file
int unlockFile(int fd)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_UNLCK;   // Unlock
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;

    return fcntl(fd, F_SETLK, &fl);
}

// ============================================================
// PATH HANDLING HELPERS
// ============================================================

static char* normalize_path(const char *path, char *normalized, size_t size)
{
    if (!path || !normalized || size == 0)
        return NULL;

    char tmp[PATH_SIZE];
    strncpy(tmp, path, sizeof(tmp));
    tmp[sizeof(tmp) - 1] = '\0';

    char *components[256];
    int count = 0;

    // Split path into components
    char *token = strtok(tmp, "/");
    while (token != NULL && count < 256) {
        if (strcmp(token, ".") == 0) {
            // Ignore current directory
        }
        else if (strcmp(token, "..") == 0) {
            // Go one level up if possible
            if (count > 0) count--;
        }
        else {
            components[count++] = token;
        }
        token = strtok(NULL, "/");
    }

    // Rebuild normalized path
    normalized[0] = '\0';

    // Preserve absolute path
    if (path[0] == '/') {
        strcat(normalized, "/");
    }

    for (int i = 0; i < count; i++) {
        if (i > 0) strcat(normalized, "/");
        strcat(normalized, components[i]);
    }

    // Special case: root directory
    if (count == 0 && path[0] == '/') {
        strcpy(normalized, "/");
    }

    return normalized;
}

// ============================================================
// Resolves user input path into an absolute server-side path
// ============================================================
int resolvePath(Session *s, const char *inputPath, char *outputPath)
{
    // Basic argument validation
    if (!s || !inputPath || !outputPath) {
        return -1;
    }

    char normalized[PATH_SIZE];
    char result[PATH_SIZE];
    result[0] = '\0';

    // --------------------------------------------------------
    // 1) Determine base directory
    // --------------------------------------------------------
    // Jusst want to be sure where are we starting from!
    char base[PATH_SIZE];

    if (inputPath[0] == '/') {
        // Absolute path from client perspective
        if (s->isLoggedIn) {
            // "/" maps to user's home directory
            snprintf(base, PATH_SIZE, "%s", s->homeDir);
        } else {
            // Just checking! This shouldn't happend because we are checking if user is loged in!
            snprintf(base, PATH_SIZE, "%s", gRootDir);
        }
    } else {
        // Relative path - current working directory
        snprintf(base, PATH_SIZE, "%s", s->currentDir);
    }

    // --------------------------------------------------------
    // 2) Build full path before normalization
    // --------------------------------------------------------
    if (strcmp(inputPath, "/") == 0 || strcmp(inputPath, "") == 0) {
        // Root or empty path - base directory
        strncpy(result, base, PATH_SIZE - 1);
        result[PATH_SIZE - 1] = '\0';
    }
    else if (inputPath[0] == '/') {
        // Absolute path 
        snprintf(result, PATH_SIZE, "%s%s", gRootDir, inputPath);
    }
    else {
        // Relative path append to base
        size_t base_len  = strlen(base);
        size_t input_len = strlen(inputPath);

        // Just ti check if path is too long once we have built it
        if (base_len + 1 + input_len + 1 > PATH_SIZE) {
            return -1; 
        }

        strcpy(result, base);
        if (base[base_len - 1] != '/') {
            strcat(result, "/");
        }
        strcat(result, inputPath);
    }

    // --------------------------------------------------------
    // 3) Normalize path
    // --------------------------------------------------------
    normalize_path(result, normalized, PATH_SIZE);

    // --------------------------------------------------------
    // 4) Sandbox check
    // --------------------------------------------------------
    if (strncmp(normalized, gRootDir, strlen(gRootDir)) != 0) {
        return -1;  // We don't need it right now because we check every path with is inside root but it dosen't change anything
    }

    // --------------------------------------------------------
    // 5) Return resolved path
    // --------------------------------------------------------
    strncpy(outputPath, normalized, PATH_SIZE);
    outputPath[PATH_SIZE - 1] = '\0';

    return 0;
}

// ============================================================
// Checks if fullPath is inside rootDir
// ============================================================
int isInsideRoot(const char *rootDir, const char *fullPath)
{
    if (!rootDir || !fullPath)
        return 0;

    size_t rootLen = strlen(rootDir);

    // Path must start with rootDir
    if (strncmp(rootDir, fullPath, rootLen) != 0) {
        return 0;
    }

    // Exact match: rootDir
    if (fullPath[rootLen] == '\0') {
        return 1;
    }

    // Subdirectory of rootDir
    // This is how we get rid of prefixes if someone tries to attack
    if (fullPath[rootLen] == '/') {
        return 1;
    }
    
    return 0;
}

// ============================================================
// Check if path is inside user's home directory
// ============================================================
int isInsideHome(const char *homeDir, const char *fullPath)
{
    if (!homeDir || !fullPath)
        return 0;

    size_t homeLen = strlen(homeDir);

    // Path must start with homeDir
    if (strncmp(homeDir, fullPath, homeLen) != 0) {
        return 0;
    }

    // Home directory itself
    if (fullPath[homeLen] == '\0') {
        return 1;
    }

    // Subdirectory inside home
    // Also same as in root dir check this way we are sure there are no prefix attacks
    if (fullPath[homeLen] == '/') {
        return 1;
    }

    return 0;
}

// ============================================================
// CREATE file or directory
// ============================================================
int fsCreate(const char *path, int permissions, int isDirectory)
{
    // Create directory
    if (isDirectory) {
        if (mkdir(path, permissions) < 0)
            return -1;
    }
    // Create regular file
    else {
        int fd = open(path, O_CREAT | O_EXCL, permissions);
        if (fd < 0)
            return -1;
        close(fd);
    }
    return 0;
}

// ============================================================
// CHMOD
// ============================================================
int fsChmod(const char *path, int permissions)
{
    return chmod(path, permissions);
}

// ============================================================
// MOVE / RENAME
// ============================================================
int fsMove(const char *src, const char *dst)
{
    return rename(src, dst);
}

// ============================================================
// READ file
// ============================================================
int fsReadFile(const char *path, char *buffer, int size, int offset)
{
    // Open file for reading
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    // Moveing to requested offset
    if (lseek(fd, offset, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }

    // Read data
    int r = read(fd, buffer, size);

    // Cleanup
    close(fd);

    return r;
}

// ============================================================
// WRITE file
// ============================================================
int fsWriteFile(const char *path, const char *data, int size, int offset)
{
    int fd;
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0700);

    if (fd >= 0) {
        // I am changeing permissions again because for some reason if I don't I get 660 permission!
        // It is said that it's done because of umask of the system!
        if (fchmod(fd, 0700) < 0) {
            close(fd);
            return -1;
        }
    } else {
        // File already exists
        if (errno != EEXIST)
            return -1;

        fd = open(path, O_WRONLY);
        if (fd < 0)
            return -1;
    }
    
    // This is just used for overwriting a file (that's if we don't specify a offset)  
    if (offset == 0) {
        if (ftruncate(fd, 0) < 0) {
            close(fd);
            return -1;
        }
    }

    // Move to requested offset
    if (lseek(fd, offset, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }

    // Write data
    int written = 0;
    if (size > 0) {
        written = write(fd, data, size);
        if (written < 0) {
            close(fd);
            return -1;
        }
    }

    // Cleanup
    close(fd);

    return written;
}