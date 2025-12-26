#ifndef CLIENT_COMMANDS_H
#define CLIENT_COMMANDS_H

#include "protocol.h"

// Parse and execute a single client command.
// Handles input parsing, protocol message creation,
// communication with the server and response handling.
// Returns 1 if the client should terminate, 0 otherwise.
int clientHandleInput(int sock, char *input);

// Client-side file transfer helpers.
// Used for both foreground and background (-b) operations.
int clientUpload(int sock, const char *localPath, const char *remotePath);
int clientDownload(int sock, const char *remotePath, const char *localPath);

// Store server connection information (IP and port).
// Needed by forked background processes to reconnect to the server.
void setGlobalServerInfo(const char *ip, int port);

// Client-side session state.
// Used only to display the prompt; the server is authoritative.
const char* getCurrentPath();
const char* getUsername();
void updateCurrentPath(const char *newPath);

// Check whether background upload/download processes are still running.
// The client must not exit while background jobs are active.
int hasActiveBackgroundProcesses(void);

#endif
