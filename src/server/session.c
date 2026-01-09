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
// ------------------------------------------------------------
int loginUser(Session *s, const char *rootDir, const char *username)
{
    s->isLoggedIn = 1;

    // Store username safely
    strncpy(s->username, username, USERNAME_SIZE);
    s->username[USERNAME_SIZE - 1] = '\0';

    // Build absolute home directory path:
    snprintf(s->homeDir, PATH_SIZE, "%s/%s", rootDir, username);

    // Start session in user's home directory
    strncpy(s->currentDir, s->homeDir, PATH_SIZE);

    return 0;
}

