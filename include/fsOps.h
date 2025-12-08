#ifndef FS_OPS_H
#define FS_OPS_H

#include "session.h"

// Resolve a client-provided path into a normalized path.
// NOTE: SAFE VERSION — preserves original project behavior.
// Returns 0 on success, -1 on error.
int resolvePath(Session *s, const char *inputPath, char *outputPath);

// Check if fullPath is inside rootDir (exact or subdirectory)
int isInsideRoot(const char *rootDir, const char *fullPath);

// Create a file or directory
int fsCreate(const char *path, int permissions, int isDirectory);

// Change permissions
int fsChmod(const char *path, int permissions);

// Move or rename a file
int fsMove(const char *src, const char *dst);

// Delete a file or directory
int fsDelete(const char *path);

// Read file content into buffer, starting from offset
int fsReadFile(const char *path, char *buffer, int size, int offset);

// Write to file with offset (creates file if missing)
int fsWriteFile(const char *path, const char *data, int size, int offset);

#endif
