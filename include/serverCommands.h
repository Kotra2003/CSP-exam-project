#ifndef SERVER_COMMANDS_H
#define SERVER_COMMANDS_H

#include "protocol.h"
#include "session.h"

// Dispatch and process a single client command.
// Sends the response to the client.
// Returns 1 if the connection should be closed, 0 otherwise.
int processCommand(int clientFd, ProtocolMessage *msg, Session *session);

// Authentication and session management
int handleLogin(int clientFd, ProtocolMessage *msg, Session *session);
int handleCreateUser(int clientFd, ProtocolMessage *msg, Session *session);

// User management extension (custom feature)
int handleDeleteUser(int clientFd, ProtocolMessage *msg, Session *session);

// File and directory management commands
int handleCreate(int clientFd, ProtocolMessage *msg, Session *session);
int handleChmod(int clientFd, ProtocolMessage *msg, Session *session);
int handleMove(int clientFd, ProtocolMessage *msg, Session *session);
int handleDelete(int clientFd, ProtocolMessage *msg, Session *session);

// Directory navigation and listing
int handleCd(int clientFd, ProtocolMessage *msg, Session *session);
int handleList(int clientFd, ProtocolMessage *msg, Session *session);

// File I/O operations
int handleRead(int clientFd, ProtocolMessage *msg, Session *session);
int handleWrite(int clientFd, ProtocolMessage *msg, Session *session);

// File transfer operations
int handleUpload(int clientFd, ProtocolMessage *msg, Session *session);
int handleDownload(int clientFd, ProtocolMessage *msg, Session *session);

#endif
