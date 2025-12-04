// In this file all most important commands on server side are added
// Clinet will everytime ask from a server to do something with protocol message and server will do it by using theses messages

#ifndef SERVER_COMMANDS_H
#define SERVER_COMMANDS_H

#include "protocol.h"
#include "session.h"

// Handle a command received from client
// Returns 0 if server should continue, 1 if exit command
int processCommand(int clientFd, ProtocolMessage *msg, Session *session);

// Login command
int handleLogin(int clientFd, ProtocolMessage *msg, Session *session);

// Create user command
int handleCreateUser(int clientFd, ProtocolMessage *msg, Session *session);

// Create file or directory
int handleCreate(int clientFd, ProtocolMessage *msg, Session *session);

// Change permissions
int handleChmod(int clientFd, ProtocolMessage *msg, Session *session);

// Move file
int handleMove(int clientFd, ProtocolMessage *msg, Session *session);

// Change directory
int handleCd(int clientFd, ProtocolMessage *msg, Session *session);

// List directory
int handleList(int clientFd, ProtocolMessage *msg, Session *session);

// Read file
int handleRead(int clientFd, ProtocolMessage *msg, Session *session);

// Write to file
int handleWrite(int clientFd, ProtocolMessage *msg, Session *session);

// Delete file
int handleDelete(int clientFd, ProtocolMessage *msg, Session *session);

int handleUpload(int clientFd, ProtocolMessage *msg, Session *session);

int handleDownload(int clientFd, ProtocolMessage *msg, Session *session);

#endif
