#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "../../include/concurrency.h"

static FileLock locks[MAX_LOCKS];

// ------------------------------------------------------------
// Init lock table (opciono, ali OK za jasnoću)
// ------------------------------------------------------------
void initLocks()
{
    for (int i = 0; i < MAX_LOCKS; i++) {
        locks[i].path[0] = '\0';
        locks[i].fd      = -1;
        locks[i].locked  = 0;
    }
}

// Vrati postojeći ulaz za path ili kreiraj novi
static FileLock* getLockEntry(const char *path)
{
    // 1) Traži postojeći
    for (int i = 0; i < MAX_LOCKS; i++) {
        if (locks[i].path[0] != '\0' && strcmp(locks[i].path, path) == 0) {
            return &locks[i];
        }
    }

    // 2) Nađi prazan slot
    for (int i = 0; i < MAX_LOCKS; i++) {
        if (locks[i].path[0] == '\0') {
            strncpy(locks[i].path, path, PATH_SIZE);
            locks[i].path[PATH_SIZE - 1] = '\0';
            locks[i].fd     = -1;
            locks[i].locked = 0;
            return &locks[i];
        }
    }

    // Nema mjesta
    return NULL;
}

// ------------------------------------------------------------
// Ekskluzivni BLOKING lock na "pravom" fajlu:
//  - ako je path regularan fajl ili ne postoji → zaključavamo baš taj fajl
//    (kreiramo ga ako ne postoji)
//  - ako je path direktorijum → zaključavamo path+".lock" (jer dir ne možeš otvoriti O_RDWR)
// ------------------------------------------------------------
int acquireFileLock(const char *path)
{
    if (!path || path[0] == '\0') return -1;

    FileLock *l = getLockEntry(path);
    if (!l) return -1;

    // Ako već imamo lock u ovom procesu → OK
    if (l->locked) {
        return 0;
    }

    struct stat st;
    int isDir = 0;

    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        isDir = 1;
    }

    char lockPath[PATH_SIZE + 10];
    const char *targetPath = path;

    if (isDir) {
        // Za direktorijume koristimo pomoćni ".lock" fajl
        snprintf(lockPath, sizeof(lockPath), "%s.lock", path);
        targetPath = lockPath;
    }

    int fd = open(targetPath, O_CREAT | O_RDWR, 0700);
    if (fd < 0) {
        perror("open lock target");
        return -1;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_WRLCK;      // ekskluzivni lock
    fl.l_whence = SEEK_SET;

    // BLOCKING: čeka dok neko drugi ne pusti lock
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl(F_SETLKW)");
        close(fd);
        return -1;
    }

    // Uspjeh
    l->fd     = fd;
    l->locked = 1;
    return 0;
}

// ------------------------------------------------------------
// Oslobađanje lock-a
// ------------------------------------------------------------
void releaseFileLock(const char *path)
{
    if (!path || path[0] == '\0') return;

    FileLock *l = getLockEntry(path);
    if (!l || !l->locked) {
        return;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;

    // Oslobodi lock (ignorišemo grešku)
    fcntl(l->fd, F_SETLK, &fl);
    close(l->fd);

    l->fd     = -1;
    l->locked = 0;
    l->path[0] = '\0';
}
