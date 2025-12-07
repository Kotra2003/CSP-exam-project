#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "../../include/session.h"

// Create an empty session (no login)
void initSession(Session *s)
{
    // Clear fields
    s->isLoggedIn = 0;
    s->username[0] = '\0';
    s->homeDir[0] = '\0';
    s->currentDir[0] = '\0';
}

// Log in a user and set paths
int loginUser(Session *s, const char *rootDir, const char *username)
{
    // Set login flag so clinet is loged in
    s->isLoggedIn = 1;

    // Copy username
    strncpy(s->username, username, USERNAME_SIZE);

    // Build home directory path: <root>/<username>
    // We need to use snprintf to be safe that text is not too long
    snprintf(s->homeDir, PATH_SIZE, "%s/%s", rootDir, username);

    // Initial worsking directory is the home directory
    strncpy(s->currentDir, s->homeDir, PATH_SIZE);

    return 0;
}

// Change directory inside session
int changeDirectory(Session *s, const char *newAbsPath)
{
    // MUST be absolute AND inside homeDir
    strncpy(s->currentDir, newAbsPath, PATH_SIZE);
    return 0;
}


// Build an absolute path from user input path
int buildFullPath(Session *s, const char *userPath, char *outputPath)
{
    // If the path is absolute
    if (userPath[0] == '/') {
        snprintf(outputPath, PATH_SIZE, "%s", userPath);
    } 
    else {
        // Relative path: currentDir + "/" + userPath
        snprintf(outputPath, PATH_SIZE, "%s/%s", s->currentDir, userPath);
    }

    return 0;  // For now accept everything; later we add security checks
}
