#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "../../include/concurrency.h"

static FileLock locks[MAX_LOCKS];

// ------------------------------------------------------------
// Initialize lock table
// ------------------------------------------------------------
void initLocks()
{
    for (int i = 0; i < MAX_LOCKS; i++) {
        locks[i].path[0] = '\0';
        locks[i].fd = -1;
        locks[i].locked = 0;
    }
}

static FileLock* getLockEntry(const char *path)
{
    for (int i = 0; i < MAX_LOCKS; i++) {
        if (strcmp(locks[i].path, path) == 0)
            return &locks[i];
    }

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
// BLOCKING EXCLUSIVE LOCK
//
// Ovo je ključ razlike:
//  - koristimo F_SETLKW → blokira dok se lock ne oslobodi
//  - NEMA fail -1 jer blokira
//  - thread-safe i fork-friendly
// ------------------------------------------------------------
int acquireFileLock(const char *path)
{
    FileLock *l = getLockEntry(path);
    if (!l) return -1;

    // već imamo lock u ovom procesu
    if (l->locked)
        return 0;

    char lockPath[PATH_SIZE + 10];
    snprintf(lockPath, sizeof(lockPath), "%s.lock", path);

    int fd = open(lockPath, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        perror("open lock file");
        return -1;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;     // EXCLUSIVE lock
    fl.l_whence = SEEK_SET;

    // 🔥 BLOCKING LOCK HERE:
    // F_SETLKW → ČEKA dok neko ne pusti lock
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl(F_SETLKW)");
        close(fd);
        return -1;
    }

    // uspjeh
    l->fd = fd;
    l->locked = 1;
    return 0;
}

void releaseFileLock(const char *path)
{
    FileLock *l = getLockEntry(path);
    if (!l || !l->locked)
        return;

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
