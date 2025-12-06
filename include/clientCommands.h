#ifndef CLIENT_COMMANDS_H
#define CLIENT_COMMANDS_H

#include "protocol.h"

int clientUpload(int sock, const char *localPath, const char *remotePath);
int clientDownload(int sock, const char *remotePath, const char *localPath);

// Generic handler for commands like:
// login, create, cd, chmod, delete, move, list, read, write
int clientSendSimple(int sock, ProtocolMessage *msg);

#endif
