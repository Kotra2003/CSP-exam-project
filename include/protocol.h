#ifndef PROTOCOL_H
#define PROTOCOL_H

// ==============================
// Command identifiers
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
#define CMD_WRITE          10
#define CMD_UPLOAD         11
#define CMD_DOWNLOAD       12
#define CMD_TRANSFER_REQ   13   // rezervisano za napredne funkcije
#define CMD_DELETE_USER    14   // nas dodatak: brisanje korisnika

// ==============================
// Status codes
// ==============================
#define STATUS_OK     0
#define STATUS_ERROR  1
#define STATUS_DENIED 2

// ==============================
#define ARG_SIZE 256

// ==============================
// Client → Server message
// ==============================
typedef struct {
    int command;
    int hasOptions;       // unused for now
    int optionValue;      // unused for now
    char arg1[ARG_SIZE];
    char arg2[ARG_SIZE];
    char arg3[ARG_SIZE];
    char data[512];       // used only for WRITE/UPLOAD (optional)
} ProtocolMessage;

// ==============================
// Server → Client response
// ==============================
typedef struct {
    int status;        // STATUS_OK, STATUS_ERROR
    int dataSize;      // > 0 means client should read additional bytes
} ProtocolResponse;

// ==============================
// Low-level transport functions
// ==============================
int sendMessage(int sock, ProtocolMessage *msg);
int receiveMessage(int sock, ProtocolMessage *msg);
int sendResponse(int sock, ProtocolResponse *res);
int receiveResponse(int sock, ProtocolResponse *res);

#endif
