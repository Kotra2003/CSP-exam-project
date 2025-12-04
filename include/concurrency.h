#ifndef CONCURRENCY_H
#define CONCURRENCY_H

#include "session.h"

#define MAX_LOCKS 64

// Structure representing a lock entry for one file
typedef struct {
    char path[PATH_SIZE];    // file path
    int readCount;           // number of readers
    int writeLock;           // 1 if write lock active
} FileLock;

// Initialize lock system
void initLocks();

// Try to acquire read lock, returns 0 if success, -1 if blocked
int acquireReadLock(const char *path);

// Release read lock
void releaseReadLock(const char *path);

// Try to acquire write lock, returns 0 if success, -1 if blocked
int acquireWriteLock(const char *path);

// Release write lock
void releaseWriteLock(const char *path);

#endif
