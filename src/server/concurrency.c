#include <stdio.h>
#include <string.h>

#include "../../include/concurrency.h"

// Global lock table
static FileLock locks[MAX_LOCKS];

// Initialize all locks
void initLocks()
{
    for (int i = 0; i < MAX_LOCKS; i++) {
        locks[i].path[0] = '\0';    // empty entry
        locks[i].readCount = 0;
        locks[i].writeLock = 0;
    }
}

// Helper: find or create lock entry for a path
static FileLock* getLock(const char *path)
{
    // 1. Try to find existing entry
    for (int i = 0; i < MAX_LOCKS; i++) {
        if (strcmp(locks[i].path, path) == 0) {
            return &locks[i];
        }
    }

    // 2. Create new entry if empty slot exists
    for (int i = 0; i < MAX_LOCKS; i++) {
        if (locks[i].path[0] == '\0') {
            strncpy(locks[i].path, path, PATH_SIZE);
            locks[i].readCount = 0;
            locks[i].writeLock = 0;
            return &locks[i];
        }
    }

    return NULL; // no free slot
}

// Cleanup lock entry if not used anymore
// It won't help us to have more than 64 active locks, but will enable new locks after 64 files
static void cleanupLock(FileLock *l)
{
    // Now every new proces will be able to use lock!
    if (l->readCount == 0 && l->writeLock == 0) {   
        l->path[0] = '\0';
    }
}

// Acquire read lock
int acquireReadLock(const char *path)
{
    FileLock *l = getLock(path);
    if (!l) {
        return -1;
    }

    // Cannot read if write is active
    if (l->writeLock == 1) {    // We need to check just for write lock in this case
        return -1;
    }

    // Safe: increase readers
    l->readCount++;
    return 0;
}

// Release read lock
void releaseReadLock(const char *path)
{
    FileLock *l = getLock(path);
    if (!l) return;

    if (l->readCount > 0) {     // ReadCount can't be negatve
        l->readCount--;
    }

    cleanupLock(l);   // free slot
}

// Acquire write lock
int acquireWriteLock(const char *path)
{
    FileLock *l = getLock(path);
    if (!l) return -1;

    // Only allow write if:
    // - NO readers
    // - NO writer
    if (l->readCount == 0 && l->writeLock == 0) {   // We check both not just read lock count
        l->writeLock = 1;
        return 0;
    }

    return -1;
}

// Release write lock
void releaseWriteLock(const char *path)
{
    FileLock *l = getLock(path);
    if (!l) return;

    l->writeLock = 0;

    cleanupLock(l);  // free slot
}
