#ifndef PROTOCOL_H
#define PROTOCOL_H

// Maximum allowed file size for upload/download (100 MB)
#define MAX_FILE_SIZE (100 * 1024 * 1024)

// ============================================================
// Command identifiers (client -> server)
// ============================================================
#define CMD_EXIT            0   // Close client connection
#define CMD_LOGIN           1   // User login
#define CMD_CREATE_USER     2   // Create new user
#define CMD_CD              3   // Change directory
#define CMD_LIST            4   // List directory contents
#define CMD_CREATE          5   // Create file or directory
#define CMD_CHMOD           6   // Change permissions
#define CMD_MOVE            7   // Move or rename file/directory
#define CMD_DELETE          8   // Delete file or directory
#define CMD_READ            9   // Read file
#define CMD_WRITE          10   // Write to file
#define CMD_UPLOAD         11   // Upload file to server
#define CMD_DOWNLOAD       12   // Download file from server

// Extra command used for testing
#define CMD_DELETE_USER    14   // Delete user

// ============================================================
// Server response status codes
// ============================================================
#define STATUS_OK     0   // Operation successful
#define STATUS_ERROR  1   // Generic error
#define STATUS_DENIED 2   // Permission denied

// Maximum length for command arguments
#define ARG_SIZE 256

// ============================================================
// Message sent from client to server
// ============================================================
typedef struct {
    int command;            // Command ID (CMD_*)
    char arg1[ARG_SIZE];    // First argument (path, username, ...)
    char arg2[ARG_SIZE];    // Second argument
    char arg3[ARG_SIZE];    // Third argument
    char data[2048];        // Payload (write / upload)
} ProtocolMessage;

// ============================================================
// Message sent from server to client
// ============================================================
typedef struct {
    int status;             // STATUS_OK / STATUS_ERROR / STATUS_DENIED
    int dataSize;           // Size of data that follows
} ProtocolResponse;

// ============================================================
// Protocol send / receive helpers
// ============================================================
int sendMessage(int sock, ProtocolMessage *msg);
int receiveMessage(int sock, ProtocolMessage *msg);
int sendResponse(int sock, ProtocolResponse *res);
int receiveResponse(int sock, ProtocolResponse *res);

#endif
