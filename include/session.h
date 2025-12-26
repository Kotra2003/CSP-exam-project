#ifndef SESSION_H
#define SESSION_H

#define USERNAME_SIZE 64
#define PATH_SIZE     4096

// Represents per-client session state.
// Stored server-side and associated with a single client connection.
typedef struct {
    int  isLoggedIn;                 // 1 if the user is authenticated
    char username[USERNAME_SIZE];    // Logged-in username
    char homeDir[PATH_SIZE];         // Absolute path to user's home directory
    char currentDir[PATH_SIZE];      // Current working directory (inside homeDir)
} Session;

// Initialize an empty session structure.
// Called when a new client connection is created.
void initSession(Session *s);

// Log in a user and initialize session paths.
// Sets username, homeDir and currentDir.
int loginUser(Session *s, const char *rootDir, const char *username);

// Update the current working directory.
// Assumes the path has already been validated.
int changeDirectory(Session *s, const char *newAbsPath);

// Build an absolute path from user input.
// This function does not enforce sandboxing;
// final validation is performed in fsOps.
int buildFullPath(Session *s, const char *userPath, char *outputPath);

#endif
