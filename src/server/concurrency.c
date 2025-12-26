#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "../../include/concurrency.h"

// Local lock table (per-process)
// Real locking is done by kernel via fcntl()
static FileLock locks[MAX_LOCKS];

// ------------------------------------------------------------
// Initialize lock table
// (Optional, but keeps state explicit and clean)
// ------------------------------------------------------------
void initLocks()
{
    for (int i = 0; i < MAX_LOCKS; i++) {
        locks[i].path[0] = '\0';
        locks[i].fd      = -1;
        locks[i].locked  = 0;
    }
}

// ------------------------------------------------------------
// Return existing lock entry for a path,
// or create a new one if it does not exist
// ------------------------------------------------------------
static FileLock* getLockEntry(const char *path)
{
    // 1) Look for an existing entry
    for (int i = 0; i < MAX_LOCKS; i++) {
        if (locks[i].path[0] != '\0' && strcmp(locks[i].path, path) == 0) {
            return &locks[i];
        }
    }

    // 2) Find a free slot
    for (int i = 0; i < MAX_LOCKS; i++) {
        if (locks[i].path[0] == '\0') {
            strncpy(locks[i].path, path, PATH_SIZE);
            locks[i].path[PATH_SIZE - 1] = '\0';
            locks[i].fd     = -1;
            locks[i].locked = 0;
            return &locks[i];
        }
    }

    // No free slot available
    return NULL;
}

// ------------------------------------------------------------
// Acquire EXCLUSIVE blocking lock for a path
//  - Regular file or non-existing path → lock the file itself
//  - Directory → lock a helper "<dir>.lock" file
// ------------------------------------------------------------
int acquireFileLock(const char *path)
{
    if (!path || path[0] == '\0') return -1;

    FileLock *l = getLockEntry(path);
    if (!l) return -1;

    // Already locked in this process
    if (l->locked) {
        return 0;
    }

    struct stat st;
    int isDir = 0;

    // Detect if path is a directory
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        isDir = 1;
    }

    char lockPath[PATH_SIZE + 10];
    const char *targetPath = path;

    if (isDir) {
        // Directories cannot be opened O_RDWR → use helper file
        snprintf(lockPath, sizeof(lockPath), "%s.lock", path);
        targetPath = lockPath;
    }

    // Open (or create) lock target
    int fd = open(targetPath, O_CREAT | O_RDWR, 0700);
    if (fd < 0) {
        perror("open lock target");
        return -1;
    }

    // Prepare exclusive write lock
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_WRLCK;     // Exclusive lock
    fl.l_whence = SEEK_SET;

    // BLOCKING lock: waits until lock becomes available
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl(F_SETLKW)");
        close(fd);
        return -1;
    }

    // Lock successfully acquired
    l->fd     = fd;
    l->locked = 1;
    return 0;
}

// ------------------------------------------------------------
// Release lock for a given path
// ------------------------------------------------------------
void releaseFileLock(const char *path)
{
    if (!path || path[0] == '\0') return;

    FileLock *l = getLockEntry(path);
    if (!l || !l->locked) {
        return;
    }

    // Prepare unlock operation
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;

    // Release lock (ignore errors)
    fcntl(l->fd, F_SETLK, &fl);
    close(l->fd);

    // Reset local lock state
    l->fd      = -1;
    l->locked  = 0;
    l->path[0] = '\0';
}
