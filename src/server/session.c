#include <stdio.h>
#include <string.h>

#include "../../include/session.h"

// ------------------------------------------------------------
// Initialize (clear) session state
// ------------------------------------------------------------
void initSession(Session *s)
{
    s->isLoggedIn = 0;
    s->username[0]   = '\0';
    s->homeDir[0]    = '\0';
    s->currentDir[0] = '\0';
}

// ------------------------------------------------------------
// Log in user and initialize session paths
// sets logged-in flag
// stores username
// builds user's home directory path
// sets current directory to home
// ------------------------------------------------------------
int loginUser(Session *s, const char *rootDir, const char *username)
{
    s->isLoggedIn = 1;

    // Store username safely
    strncpy(s->username, username, USERNAME_SIZE);
    s->username[USERNAME_SIZE - 1] = '\0';

    // Build absolute home directory path: <rootDir>/<username>
    snprintf(s->homeDir, PATH_SIZE, "%s/%s", rootDir, username);

    // Start session in user's home directory
    strncpy(s->currentDir, s->homeDir, PATH_SIZE);

    return 0;
}

// ------------------------------------------------------------
// Change current directory inside session
// (path must already be validated elsewhere)
// ------------------------------------------------------------
// int changeDirectory(Session *s, const char *newAbsPath)
// {
//     strncpy(s->currentDir, newAbsPath, PATH_SIZE);
//     return 0;
// }

// // ------------------------------------------------------------
// // Build absolute path from user-provided path
// // ------------------------------------------------------------
// int buildFullPath(Session *s, const char *userPath, char *outputPath)
// {
//     if (!s || !userPath || !outputPath)
//         return -1;

//     size_t len1 = strlen(s->currentDir);
//     size_t len2 = strlen(userPath);

//     // +1 for '/', +1 for null terminator
//     if (len1 + 1 + len2 + 1 > PATH_SIZE)
//         return -1;

//     memcpy(outputPath, s->currentDir, len1);
//     outputPath[len1] = '/';
//     memcpy(outputPath + len1 + 1, userPath, len2);
//     outputPath[len1 + 1 + len2] = '\0';

//     return 0;
// }
