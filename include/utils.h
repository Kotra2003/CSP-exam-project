#ifndef UTILS_H
#define UTILS_H

// Remove trailing newline (if present)
void removeNewline(char *str);

// Check if a string contains only numeric digits (0–9)
int isNumeric(const char *str);

// Safely join two paths: base + "/" + child  (caller must ensure buffer is large enough)
void joinPaths(const char *base, const char *child, char *output);

// Generate a small random ID (for transfer_request)
int generateId();

// Check if file or directory exists
int fileExists(const char *path);

#endif
