#ifndef CLIENT_COMMANDS_H
#define CLIENT_COMMANDS_H

#include "protocol.h"

int clientHandleInput(int sock, char *input);

// upload/download implementirani u networkClient.c
int clientUpload(int sock, const char *localPath, const char *remotePath);
int clientDownload(int sock, const char *remotePath, const char *localPath);

#endif
