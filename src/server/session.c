#include <stdio.h>
#include <string.h>

#include "../../include/session.h"

// Clear session state
void initSession(Session *s)
{
    s->isLoggedIn = 0;
    s->username[0] = '\0';
    s->homeDir[0] = '\0';
    s->currentDir[0] = '\0';
}

// Log in user and construct home directory
int loginUser(Session *s, const char *rootDir, const char *username)
{
    s->isLoggedIn = 1;

    strncpy(s->username, username, USERNAME_SIZE);
    s->username[USERNAME_SIZE - 1] = '\0';

    // Build home dir path
    snprintf(s->homeDir, PATH_SIZE, "%s/%s", rootDir, username);

    // Start in home directory
    strncpy(s->currentDir, s->homeDir, PATH_SIZE);

    return 0;
}

// Change working directory inside session
int changeDirectory(Session *s, const char *newAbsPath)
{
    strncpy(s->currentDir, newAbsPath, PATH_SIZE);
    return 0;
}

// Build absolute path from user-provided path (no sandbox check here)
int buildFullPath(Session *s, const char *userPath, char *outputPath)
{
    if (userPath[0] == '/') {
        snprintf(outputPath, PATH_SIZE, "%s", userPath);
    } else {
        snprintf(outputPath, PATH_SIZE, "%s/%s", s->currentDir, userPath);
    }
    return 0;
}
