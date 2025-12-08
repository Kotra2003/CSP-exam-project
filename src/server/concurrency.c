#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "../../include/concurrency.h"

static FileLock locks[MAX_LOCKS];

// ------------------------------------------------------------
// Initialize local lock table
// ------------------------------------------------------------
void initLocks()
{
    for (int i = 0; i < MAX_LOCKS; i++) {
        locks[i].path[0] = '\0';
        locks[i].fd = -1;
        locks[i].locked = 0;
    }
}

// ------------------------------------------------------------
// Find or create lock entry for a path
// ------------------------------------------------------------
static FileLock* getLockEntry(const char *path)
{
    // Find existing
    for (int i = 0; i < MAX_LOCKS; i++) {
        if (strcmp(locks[i].path, path) == 0)
            return &locks[i];
    }

    // Create new
    for (int i = 0; i < MAX_LOCKS; i++) {
        if (locks[i].path[0] == '\0') {
            strncpy(locks[i].path, path, PATH_SIZE);
            locks[i].fd = -1;
            locks[i].locked = 0;
            return &locks[i];
        }
    }
    return NULL;
}

// ------------------------------------------------------------
// Acquire EXCLUSIVE lock using fcntl
// ------------------------------------------------------------
int acquireFileLock(const char *path)
{
    FileLock *l = getLockEntry(path);
    if (!l) return -1;

    // Already locked in THIS process
    if (l->locked == 1) {
        return 0;
    }

    // Create a sidecar .lock file
    char lockPath[PATH_SIZE + 10];
    snprintf(lockPath, sizeof(lockPath), "%s.lock", path);

    int fd = open(lockPath, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        perror("open lock file");
        return -1;
    }

    // Prepare exclusive lock
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;   // exclusive lock
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    // NON-BLOCKING attempt
    if (fcntl(fd, F_SETLK, &fl) < 0) {
        close(fd);
        return -1;  // lock is already held by another process
    }

    // Success
    l->fd = fd;
    l->locked = 1;

    return 0;
}

// ------------------------------------------------------------
// Release file lock
// ------------------------------------------------------------
void releaseFileLock(const char *path)
{
    FileLock *l = getLockEntry(path);
    if (!l || l->locked == 0) return;

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;

    fcntl(l->fd, F_SETLK, &fl);
    close(l->fd);

    l->fd = -1;
    l->locked = 0;
    l->path[0] = '\0';
}
