#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_FILE_SIZE (100 * 1024 * 1024) // 100 MB

// ==============================
// Command identifiers
// Each value represents a client request type
// ==============================
#define CMD_EXIT            0   // Terminate client/server
#define CMD_LOGIN           1   // Login as a user
#define CMD_CREATE_USER     2   // Create a new user
#define CMD_CD              3   // Change directory
#define CMD_LIST            4   // List directory contents
#define CMD_CREATE          5   // Create file or directory
#define CMD_CHMOD           6   // Change permissions
#define CMD_MOVE            7   // Move or rename file/directory
#define CMD_DELETE          8   // Delete file or directory
#define CMD_READ            9   // Read file content
#define CMD_WRITE          10   // Write to file
#define CMD_UPLOAD         11   // Upload file from client to server
#define CMD_DOWNLOAD       12   // Download file from server to client

#define CMD_DELETE_USER    14   // Custom extension: delete user

// ==============================
// Status codes returned by server
// ==============================
#define STATUS_OK     0   // Operation completed successfully
#define STATUS_ERROR  1   // Generic error
#define STATUS_DENIED 2   // Permission or access denied

// ==============================
// Maximum size for command arguments
// ==============================
#define ARG_SIZE 256

// ==============================
// Client → Server message
// Sent for every client command
// ==============================
typedef struct {
    int command;            // Command identifier (CMD_*)
    int hasOptions;         
    int optionValue;        
    char arg1[ARG_SIZE];    // First argument (e.g. path, username)
    char arg2[ARG_SIZE];    // Second argument
    char arg3[ARG_SIZE];    // Third argument (if needed)
    char data[2048];        // Payload for WRITE / UPLOAD operations
} ProtocolMessage;

// ==============================
// Server → Client response
// ==============================
typedef struct {
    int status;             // STATUS_OK, STATUS_ERROR, STATUS_DENIED
    int dataSize;           // >0 means additional data will follow
} ProtocolResponse;

// ==============================
// Low-level protocol transport
// These functions guarantee full send/receive
// ==============================
int sendMessage(int sock, ProtocolMessage *msg);
int receiveMessage(int sock, ProtocolMessage *msg);
int sendResponse(int sock, ProtocolResponse *res);
int receiveResponse(int sock, ProtocolResponse *res);

#endif
