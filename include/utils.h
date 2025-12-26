#ifndef UTILS_H
#define UTILS_H

#include "session.h"

// Remove trailing newline character from a string, if present.
// Useful when reading input with fgets().
void removeNewline(char *str);

// Check whether a string contains only numeric characters (0–9).
// Used for validating permission values and numeric arguments.
int isNumeric(const char *str);

// Safely join two filesystem paths: base + "/" + child.
// The caller must ensure that the output buffer is large enough.
void joinPaths(const char *base, const char *child, char *output);

// Generate a small random identifier.
// Used for transfer_request handling.
int generateId();

// Check whether a file or directory exists on the filesystem.
int fileExists(const char *path);

// Recursively remove a file or directory tree (rm -rf style).
// Used for delete operations on directories.
int removeRecursive(const char *path);

#endif
