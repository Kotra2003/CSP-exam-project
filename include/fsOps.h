#ifndef FS_OPS_H
#define FS_OPS_H

#include "session.h"

// ============================================================
// LOCK API — fcntl() read/write locks
// ============================================================

// Acquire shared (read) lock — blocks until available
int lockFileRead(int fd);

// Acquire exclusive (write) lock — blocks until available
int lockFileWrite(int fd);

// Unlock file
int unlockFile(int fd);

// ============================================================
// PATH + FILE OPS
// ============================================================

int resolvePath(Session *s, const char *inputPath, char *outputPath);
int isInsideRoot(const char *rootDir, const char *fullPath);
int isInsideHome(const char *homeDir, const char *fullPath);  // DODAJ OVO!

int fsCreate(const char *path, int permissions, int isDirectory);
int fsChmod(const char *path, int permissions);
int fsMove(const char *src, const char *dst);

int fsReadFile(const char *path, char *buffer, int size, int offset);
int fsWriteFile(const char *path, const char *data, int size, int offset);

#endif