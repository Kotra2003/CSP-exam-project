#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../include/utils.h"

// Remove newline at end of string
void removeNewline(char *str)
{
    int len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

// Check if string contains only digits, if yes we can see it as a numerical and we can cast it
int isNumeric(const char *str)
{
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9')
            return 0; // false
    }
    return 1; // true
}

// Join base directory with child path (for example when we want to cd to diff directory)
void joinPaths(const char *base, const char *child, char *output)
{
    snprintf(output, 512, "%s/%s", base, child);
}

// Generate a random ID (Only used in trafnsfer request form one user to another)
int generateId()
{
    return rand() % 1000000; // simple random number
}

// Check if file exists
int fileExists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}
