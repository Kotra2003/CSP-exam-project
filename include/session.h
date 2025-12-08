#ifndef SESSION_H
#define SESSION_H

#define USERNAME_SIZE 64
#define PATH_SIZE     4096

// Represents per-client session state
typedef struct {
    int  isLoggedIn;                 // 1 if user authenticated
    char username[USERNAME_SIZE];    // logged-in username
    char homeDir[PATH_SIZE];         // absolute path to user's home directory
    char currentDir[PATH_SIZE];      // current working directory (always inside homeDir)
} Session;

// Initialize an empty session
void initSession(Session *s);

// Log in a user (sets username, homeDir, currentDir)
int loginUser(Session *s, const char *rootDir, const char *username);

// Change current working directory (NOT sandbox-checked here)
int changeDirectory(Session *s, const char *newAbsPath);

// Build absolute path from user input (does NOT handle sandboxing)
// Used internally — full checking happens in fsOps
int buildFullPath(Session *s, const char *userPath, char *outputPath);

#endif
