#ifndef FS_OPS_H
#define FS_OPS_H

#include "session.h"

// fcntl-based file locking primitives.
// Used to synchronize concurrent read/write access to files.
int lockFileRead(int fd);     // Blocking shared (read) lock
int lockFileWrite(int fd);    // Blocking exclusive (write) lock
int unlockFile(int fd);       // Release the file lock

// Path resolution and sandbox enforcement.
// Ensures that all operations stay inside the server root
// and, when required, inside the user's home directory.
int resolvePath(Session *s, const char *inputPath, char *outputPath);
int isInsideRoot(const char *rootDir, const char *fullPath);
int isInsideHome(const char *homeDir, const char *fullPath);

// Filesystem operations executed on the server side.
// All paths are assumed to be already validated and resolved.
int fsCreate(const char *path, int permissions, int isDirectory);
int fsChmod(const char *path, int permissions);
int fsMove(const char *src, const char *dst);

// File I/O operations with optional offset.
// Offsets allow partial reads and writes as required by the specification.
int fsReadFile(const char *path, char *buffer, int size, int offset);
int fsWriteFile(const char *path, const char *data, int size, int offset);

#endif
