#ifndef SESSION_H
#define SESSION_H

#define USERNAME_SIZE 64
#define PATH_SIZE     4096

// Server-side session data for one client
typedef struct {
    int  isLoggedIn;                 // 1 if user is logged in
    char username[USERNAME_SIZE];    // Username
    char homeDir[PATH_SIZE];         // User home directory
    char currentDir[PATH_SIZE];      // Current directory
} Session;

// Initialize empty session
void initSession(Session *s);

// Set session data after successful login
int loginUser(Session *s, const char *rootDir, const char *username);

#endif
