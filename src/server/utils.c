#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../include/utils.h"
#include "../../include/session.h"

// Remove newline at the end of a string (if exists)
void removeNewline(char *str)
{
    int len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

// Check if a string contains only digits
int isNumeric(const char *str)
{
    if (!str || *str == '\0')
        return 0;

    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9')
            return 0;
    }
    return 1;
}

// Safely join two paths
void joinPaths(const char *base, const char *child, char *output)
{
    // Caller ensures output buffer is big enough
    if (child[0] == '\0') {
        snprintf(output, PATH_SIZE, "%s", base);
    } else {
        snprintf(output, PATH_SIZE, "%s/%s", base, child);
    }
}

// Simple random ID
int generateId()
{
    return rand() % 1000000;
}

// Check if file/directory exists
int fileExists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0);
}
