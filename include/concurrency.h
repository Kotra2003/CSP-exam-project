#ifndef CONCURRENCY_H
#define CONCURRENCY_H

#include "session.h"

#define MAX_LOCKS 128   // Maximum number of lock entries per process

// Per-process lock bookkeeping.
// Actual mutual exclusion is enforced by kernel-level fcntl locks.
typedef struct {
    char path[PATH_SIZE];   // Path of the file or directory being locked
    int fd;                 // File descriptor holding the fcntl lock
    int locked;             // 1 if the lock is active in this process
} FileLock;

// Initialize the local lock table.
// Used for clarity and safety.
void initLocks();

// Acquire a blocking exclusive lock for the given path.
// The call blocks until the lock becomes available.
int acquireFileLock(const char *path);

// Release the lock associated with the given path.
void releaseFileLock(const char *path);

#endif
