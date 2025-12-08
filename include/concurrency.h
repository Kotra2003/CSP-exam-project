#ifndef CONCURRENCY_H
#define CONCURRENCY_H

#include "session.h"

#define MAX_LOCKS 128

typedef struct {
    char path[PATH_SIZE];   // file path being locked
    int fd;                 // lock file descriptor
    int locked;             // 1 if lock held by THIS process
} FileLock;

// Initialize local lock table
void initLocks();

// Acquire EXCLUSIVE lock. Returns 0 = success, -1 = fail.
int acquireFileLock(const char *path);

// Release EXCLUSIVE lock
void releaseFileLock(const char *path);

#endif
