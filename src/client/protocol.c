// Client-side protocol helpers
// Uses fixed-size messages and sendAll / recvAll

#include <stdio.h>
#include <unistd.h>

#include "../../include/network.h"
#include "../../include/protocol.h"

// ------------------------------------------------------------
// Send ProtocolMessage
// ------------------------------------------------------------
int sendMessage(int sock, ProtocolMessage *msg)
{
    if (sendAll(sock, msg, sizeof(ProtocolMessage)) < 0) {
        perror("sendMessage");
        return -1;
    }

    return 0;
}

// ------------------------------------------------------------
// Receive ProtocolMessage
// ------------------------------------------------------------
int receiveMessage(int sock, ProtocolMessage *msg)
{
    if (recvAll(sock, msg, sizeof(ProtocolMessage)) < 0) {
        perror("receiveMessage");
        return -1;
    }

    return 0;
}

// ------------------------------------------------------------
// Send ProtocolResponse
// ------------------------------------------------------------
int sendResponse(int sock, ProtocolResponse *res)
{
    if (sendAll(sock, res, sizeof(ProtocolResponse)) < 0) {
        perror("sendResponse");
        return -1;
    }

    return 0;
}

// ------------------------------------------------------------
// Receive ProtocolResponse
// ------------------------------------------------------------
int receiveResponse(int sock, ProtocolResponse *res)
{
    if (recvAll(sock, res, sizeof(ProtocolResponse)) < 0) {
        perror("receiveResponse");
        return -1;
    }

    return 0;
}
