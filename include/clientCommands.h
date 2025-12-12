#ifndef CLIENT_COMMANDS_H
#define CLIENT_COMMANDS_H

#include "protocol.h"

int clientHandleInput(int sock, char *input);

// upload/download
int clientUpload(int sock, const char *localPath, const char *remotePath);
int clientDownload(int sock, const char *remotePath, const char *localPath);

// Globalne funkcije
void setGlobalServerInfo(const char *ip, int port);
const char* getCurrentPath();
const char* getUsername();  // DODAJ
void updateCurrentPath(const char *newPath);
int hasActiveBackgroundProcesses(void);

#endif