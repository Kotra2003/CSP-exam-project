#ifndef SESSION_H
#define SESSION_H

#define USERNAME_SIZE 64
#define PATH_SIZE 1024

// Structure representing one user's session
typedef struct {
    int isLoggedIn;                    // 1 if user logged in
    char username[USERNAME_SIZE];      // logged user name
    char homeDir[PATH_SIZE];           // absolute path to home directory
    char currentDir[PATH_SIZE];        // current working directory
} Session;

// Initialize empty session (no user logged)
void initSession(Session *s);

// Log in a user (set username, homeDir, currentDir)
// Returns 0 on success, -1 if invalid
int loginUser(Session *s, const char *rootDir, const char *username);

// Change current working directory of this session
// Returns 0 on success, -1 on error
int changeDirectory(Session *s, const char *newPath);

// Build absolute path inside user's sandbox
// Returns 0 on success, -1 if outside sandbox
int buildFullPath(Session *s, const char *userPath, char *outputPath);

#endif
