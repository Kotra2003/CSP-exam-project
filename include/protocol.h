// Protocol definitions for client-server communication
#ifndef PROTOCOL_H
#define PROTOCOL_H

// ==============================
// Correct command identifiers
// ==============================
#define CMD_EXIT            0
#define CMD_LOGIN           1
#define CMD_CREATE_USER     2
#define CMD_CD              3
#define CMD_LIST            4
#define CMD_CREATE          5
#define CMD_CHMOD           6
#define CMD_MOVE            7
#define CMD_DELETE          8
#define CMD_READ            9
#define CMD_WRITE           10
#define CMD_UPLOAD          11
#define CMD_DOWNLOAD        12
#define CMD_TRANSFER_REQ    13  // optional advanced feature

// ==============================
// Server response status codes
// ==============================
#define STATUS_OK           0
#define STATUS_ERROR        1
#define STATUS_DENIED       2

// ==============================
// Protocol message sizes
// ==============================
#define ARG_SIZE 256

// ==============================
// Message from client to server
// ==============================
typedef struct {
    int command;                  // command ID
    int hasOptions;               // UNUSED for now (always 0)
    int optionValue;              // UNUSED for now
    char arg1[ARG_SIZE];          // first argument
    char arg2[ARG_SIZE];          // second argument
    char arg3[ARG_SIZE];          // third argument
} ProtocolMessage;

// ==============================
// Response from server
// ==============================
typedef struct {
    int status;                   // STATUS_OK / STATUS_ERROR
    int dataSize;                 // If server sends file content
} ProtocolResponse;

// ==============================
// Function prototypes
// ==============================
int sendMessage(int sock, ProtocolMessage *msg);
int receiveMessage(int sock, ProtocolMessage *msg);

int sendResponse(int sock, ProtocolResponse *res);
int receiveResponse(int sock, ProtocolResponse *res);

#endif
