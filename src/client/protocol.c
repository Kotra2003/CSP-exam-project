// All functions used here are identical to the server-side ones
// Communication is done using fixed-size protocol structures
// sendAll / recvAll ensure full transmission

#include <stdio.h>
#include <unistd.h>

#include "../../include/network.h"   // sendAll / recvAll
#include "../../include/protocol.h"  // ProtocolMessage / ProtocolResponse

// ------------------------------------------------------------
// Send a ProtocolMessage to the server
// ------------------------------------------------------------
int sendMessage(int sock, ProtocolMessage *msg)
{
    // Send the entire ProtocolMessage structure
    // Using fixed-size messages simplifies the protocol
    if (sendAll(sock, msg, sizeof(ProtocolMessage)) < 0) {
        perror("sendMessage");
        return -1;
    }

    return 0;
}

// ------------------------------------------------------------
// Receive a ProtocolMessage from the server
// ------------------------------------------------------------
int receiveMessage(int sock, ProtocolMessage *msg)
{
    // Receive the full ProtocolMessage structure
    if (recvAll(sock, msg, sizeof(ProtocolMessage)) < 0) {
        perror("receiveMessage");
        return -1;
    }

    return 0;
}

// ------------------------------------------------------------
// Send a ProtocolResponse
// (Defined for symmetry with server-side code)
// ------------------------------------------------------------
int sendResponse(int sock, ProtocolResponse *res)
{
    // Send the ProtocolResponse structure
    if (sendAll(sock, res, sizeof(ProtocolResponse)) < 0) {
        perror("sendResponse");
        return -1;
    }

    return 0;
}

// ------------------------------------------------------------
// Receive a ProtocolResponse from the server
// ------------------------------------------------------------
int receiveResponse(int sock, ProtocolResponse *res)
{
    // Receive the full ProtocolResponse structure
    if (recvAll(sock, res, sizeof(ProtocolResponse)) < 0) {
        perror("receiveResponse");
        return -1;
    }

    return 0;
}
