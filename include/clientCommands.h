#ifndef CLIENT_COMMANDS_H
#define CLIENT_COMMANDS_H

#include "protocol.h"

// Parse and execute one client command.
// Returns 1 if the client should exit.
int clientHandleInput(int sock, char *input);

// Client-side helpers for upload and download.
// Used for normal and background (-b) transfers.
int clientUpload(int sock, const char *localPath, const char *remotePath);
int clientDownload(int sock, const char *remotePath, const char *localPath);

// Store server IP and port for background processes.
void setGlobalServerInfo(const char *ip, int port);

// Client-side state, used only for the prompt.
const char* getCurrentPath();
const char* getUsername();
void updateCurrentPath(const char *newPath);

// Check if background transfers are still running.
int hasActiveBackgroundProcesses(void);

#endif
