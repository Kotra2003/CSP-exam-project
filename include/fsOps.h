#ifndef FS_OPS_H
#define FS_OPS_H

#include "session.h"

// File locking (fcntl)
int lockFileRead(int fd);     // shared lock
int lockFileWrite(int fd);    // exclusive lock
int unlockFile(int fd);       // unlock

// Path handling and sandbox checks
int resolvePath(Session *s, const char *inputPath, char *outputPath);
int isInsideRoot(const char *rootDir, const char *fullPath);
int isInsideHome(const char *homeDir, const char *fullPath);

// Filesystem operations (no locking here)
int fsCreate(const char *path, int permissions, int isDirectory);
int fsChmod(const char *path, int permissions);
int fsMove(const char *src, const char *dst);
int fsReadFile(const char *path, char *buffer, int size, int offset);
int fsWriteFile(const char *path, const char *data, int size, int offset);

#endif
