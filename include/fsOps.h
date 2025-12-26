#ifndef FS_OPS_H
#define FS_OPS_H

#include "session.h"

// ============================================================
// File locking primitives (fcntl-based)
// ============================================================
int lockFileRead(int fd);     // Acquire shared (read) lock
int lockFileWrite(int fd);    // Acquire exclusive (write) lock
int unlockFile(int fd);       // Release any lock

// ============================================================
// Path resolution and sandbox enforcement
// ============================================================
int resolvePath(Session *s, const char *inputPath, char *outputPath);
int isInsideRoot(const char *rootDir, const char *fullPath);
int isInsideHome(const char *homeDir, const char *fullPath);

// ============================================================
// Filesystem operations
// NOTE: These do NOT handle locking - caller must lock files
// ============================================================
int fsCreate(const char *path, int permissions, int isDirectory);
int fsChmod(const char *path, int permissions);
int fsMove(const char *src, const char *dst);
int fsReadFile(const char *path, char *buffer, int size, int offset);
int fsWriteFile(const char *path, const char *data, int size, int offset);

#endif