#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>   // za opendir/readdir
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

// =======================================================
// removeRecursive
//   Rekurzivno briše fajl ili direktorij (kao "rm -rf").
//   Koristi se za brisanje korisničkog home-a u rootDir.
// =======================================================
int removeRecursive(const char *path)
{
    // Remove associated .lock file if present
    char lockFile[PATH_SIZE + 10];
    snprintf(lockFile, sizeof(lockFile), "%s.lock", path);
    unlink(lockFile); // ignore errors

    struct stat st;
    if (lstat(path, &st) < 0) {
        perror("lstat");
        return -1;
    }

    // Ako nije direktorij → obriši kao običan fajl
    if (!S_ISDIR(st.st_mode)) {
        if (unlink(path) < 0) {
            perror("unlink");
            return -1;
        }
        return 0;
    }

    // Direktorij → iteriraj kroz sadržaj
    DIR *dir = opendir(path);
    if (!dir) {
        perror("opendir");
        return -1;
    }

    struct dirent *entry;
    char childPath[PATH_SIZE];

    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        snprintf(childPath, PATH_SIZE, "%s/%s", path, entry->d_name);
        if (removeRecursive(childPath) < 0) {
            // nastavljamo pokušavati dalje, ali prijavimo grešku
            // možeš ovdje dodati dodatni logging ako želiš
            continue;
        }
    }

    closedir(dir);

    if (rmdir(path) < 0) {
        perror("rmdir");
        return -1;
    }

    return 0;
}
