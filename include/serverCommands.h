#ifndef SERVER_COMMANDS_H
#define SERVER_COMMANDS_H

#include "protocol.h"
#include "session.h"

// Process a client command.
// Returns 1 if server should close connection, 0 otherwise.
int processCommand(int clientFd, ProtocolMessage *msg, Session *session);

// Authentication
int handleLogin(int clientFd, ProtocolMessage *msg, Session *session);
int handleCreateUser(int clientFd, ProtocolMessage *msg, Session *session);

// File / directory management
int handleCreate(int clientFd, ProtocolMessage *msg, Session *session);
int handleChmod(int clientFd, ProtocolMessage *msg, Session *session);
int handleMove(int clientFd, ProtocolMessage *msg, Session *session);
int handleDelete(int clientFd, ProtocolMessage *msg, Session *session);

// Navigation
int handleCd(int clientFd, ProtocolMessage *msg, Session *session);
int handleList(int clientFd, ProtocolMessage *msg, Session *session);

// File I/O
int handleRead(int clientFd, ProtocolMessage *msg, Session *session);
int handleWrite(int clientFd, ProtocolMessage *msg, Session *session);

// File Transfer
int handleUpload(int clientFd, ProtocolMessage *msg, Session *session);
int handleDownload(int clientFd, ProtocolMessage *msg, Session *session);

#endif
