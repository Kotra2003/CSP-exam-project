// In this file we use commands that are used to work with file system
// Through project server commands will call these commands/functions to do a job that clinet wants

#ifndef FS_OPS_H
#define FS_OPS_H

#include "session.h"

// Resolve a path and build an absolute safe path inside root
// Returns 0 on success, -1 if outside sandbox
int resolvePath(Session *s, const char *inputPath, char *outputPath);

// Check if a path stays inside root directory
int isInsideRoot(const char *rootDir, const char *fullPath);

// Create file or directory
int fsCreate(const char *path, int permissions, int isDirectory);

// Change permissions
int fsChmod(const char *path, int permissions);

// Move file from src to dst
int fsMove(const char *src, const char *dst);

// Delete a file or directory
int fsDelete(const char *path);

// Read file into buffer (starting from offset)
// Returns number of bytes read or -1 on error
int fsReadFile(const char *path, char *buffer, int size, int offset);

// Write data into file at specific offset
// If file does not exist, create with default permission 0700
int fsWriteFile(const char *path, const char *data, int size, int offset);

#endif
