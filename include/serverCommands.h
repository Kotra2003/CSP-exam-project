#ifndef SERVER_COMMANDS_H
#define SERVER_COMMANDS_H

#include "protocol.h"
#include "session.h"

// Main dispatcher for client commands.
// Executes the requested operation and sends a response.
// Returns 1 if the client connection should be closed.
int processCommand(int clientFd, ProtocolMessage *msg, Session *session);

// ============================================================
// Authentication / session handling
// ============================================================
int handleLogin(int clientFd, ProtocolMessage *msg, Session *session);
int handleCreateUser(int clientFd, ProtocolMessage *msg, Session *session);

// Extra command used for testing
int handleDeleteUser(int clientFd, ProtocolMessage *msg, Session *session);

// ============================================================
// File and directory management
// ============================================================
int handleCreate(int clientFd, ProtocolMessage *msg, Session *session);
int handleChmod(int clientFd, ProtocolMessage *msg, Session *session);
int handleMove(int clientFd, ProtocolMessage *msg, Session *session);
int handleDelete(int clientFd, ProtocolMessage *msg, Session *session);

// ============================================================
// Directory navigation and listing
// ============================================================
int handleCd(int clientFd, ProtocolMessage *msg, Session *session);
int handleList(int clientFd, ProtocolMessage *msg, Session *session);

// ============================================================
// File read / write operations
// ============================================================
int handleRead(int clientFd, ProtocolMessage *msg, Session *session);
int handleWrite(int clientFd, ProtocolMessage *msg, Session *session);

// ============================================================
// File transfer (client <-> server)
// ============================================================
int handleUpload(int clientFd, ProtocolMessage *msg, Session *session);
int handleDownload(int clientFd, ProtocolMessage *msg, Session *session);

#endif
