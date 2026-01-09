#ifndef UTILS_H
#define UTILS_H

#include "session.h"

// Remove newline at the end of a string
void removeNewline(char *str);

// Check if string contains only digits
int isNumeric(const char *str);

// Join two paths: base/child
void joinPaths(const char *base, const char *child, char *output);

// Generate simple random ID
int generateId();

// Check if file or directory exists
int fileExists(const char *path);

// Recursively delete file or directory
int removeRecursive(const char *path);

#endif
