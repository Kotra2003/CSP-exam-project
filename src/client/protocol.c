#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../include/network.h"   
#include "../../include/protocol.h"  

// ------------------------------------------------------------
// Send a complete ProtocolMessage structure
// ------------------------------------------------------------
int sendMessage(int sock, ProtocolMessage *msg)
{
    if (!msg) {
        fprintf(stderr, "sendMessage: NULL pointer\n");
        return -1;
    }

    // Send the entire message as fixed-size raw bytes
    if (sendAll(sock, msg, sizeof(ProtocolMessage)) < 0) {
        perror("sendMessage");
        return -1;
    }

    return 0;
}

// ------------------------------------------------------------
// Receive a complete ProtocolMessage structure
// ------------------------------------------------------------
int receiveMessage(int sock, ProtocolMessage *msg)
{
    if (!msg) {
        fprintf(stderr, "receiveMessage: NULL pointer\n");
        return -1;
    }

    // Receive the entire fixed-size message
    if (recvAll(sock, msg, sizeof(ProtocolMessage)) < 0) {
        perror("receiveMessage");
        return -1;
    }

    return 0;
}

// ------------------------------------------------------------
// Send a ProtocolResponse to client
// ------------------------------------------------------------
int sendResponse(int sock, ProtocolResponse *res)
{
    if (!res) {
        fprintf(stderr, "sendResponse: NULL pointer\n");
        return -1;
    }

    // Send full response structure
    if (sendAll(sock, res, sizeof(ProtocolResponse)) < 0) {
        perror("sendResponse");
        return -1;
    }

    return 0;
}

// ------------------------------------------------------------
// Receive a ProtocolResponse from client
// ------------------------------------------------------------
int receiveResponse(int sock, ProtocolResponse *res)
{
    if (!res) {
        fprintf(stderr, "receiveResponse: NULL pointer\n");
        return -1;
    }

    // Receive full response structure
    if (recvAll(sock, res, sizeof(ProtocolResponse)) < 0) {
        perror("receiveResponse");
        return -1;
    }

    return 0;
}
