#ifndef UTILS_H
#define UTILS_H

// Remove newline from a string (if exists)
void removeNewline(char *str);

// Check if string is numeric (for permissions)
int isNumeric(const char *str);

// Join two paths safely into output buffer
void joinPaths(const char *base, const char *child, char *output);

// Generate a small random ID (used for transfer_request)
int generateId();

// Check if file exists
int fileExists(const char *path);

#endif
