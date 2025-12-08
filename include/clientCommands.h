#ifndef CLIENT_COMMANDS_H
#define CLIENT_COMMANDS_H

#include "protocol.h"

// Upload local file → remote path
int clientUpload(int sock, const char *localPath, const char *remotePath);

// Download remote file → local path
int clientDownload(int sock, const char *remotePath, const char *localPath);

// Generic sender for simple commands (login, create, cd, chmod, delete, move, list, read, write)
int clientSendSimple(int sock, ProtocolMessage *msg);

#endif
