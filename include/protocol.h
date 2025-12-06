// The whole point of this file is to define a strict rule of communication
// If we follow it correctly we will always know what client can send to server and what it can recive from it
#ifndef PROTOCOL_H
#define PROTOCOL_H

// Command identifiers (With integers we can define functions insted of always writting them)
#define CMD_LOGIN          1
#define CMD_CREATE_USER    2
#define CMD_CREATE         3
#define CMD_CHMOD          4
#define CMD_MOVE           5
#define CMD_UPLOAD         6
#define CMD_DOWNLOAD       7
#define CMD_CD             8
#define CMD_LIST           9
#define CMD_READ           10
#define CMD_WRITE          11
#define CMD_DELETE         12
#define CMD_EXIT           13
#define CMD_TRANSFER_REQ   14
#define CMD_ACCEPT         15
#define CMD_REJECT         16

// Simple status responses from the server (Same with integers we can send the status from server to client)
#define STATUS_OK          1
#define STATUS_ERROR       2
#define STATUS_DENIED      3

// Maximum size for a string argument (like path or username)
#define ARG_SIZE 256

// Structure representing a request from client to server
typedef struct {
    int command;                  // command ID
    int hasOptions;               // 1 if option is used (it can be npr. -offset)
    int optionValue;              // option value
    char arg1[ARG_SIZE];          // first argument (path or username)
    char arg2[ARG_SIZE];          // second argument
    char arg3[ARG_SIZE];          // third argument
} ProtocolMessage;

// Structure representing a server response
typedef struct {
    int status;                   // STATUS_OK, STATUS_ERROR
    int dataSize;                 // number of bytes to follow (used for file transfers)
} ProtocolResponse;

// Send and receive messages (request) over a socket
int sendMessage(int sock, ProtocolMessage *msg);
int receiveMessage(int sock, ProtocolMessage *msg);

// Send and receive responses over a socket
int sendResponse(int sock, ProtocolResponse *res);
int receiveResponse(int sock, ProtocolResponse *res);

#endif
