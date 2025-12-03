// In all of these functions we are using sendAll and reciveAll to be sure to send all a get all data
// There are 4 possibilities:
// 1. Sending message to client
// 2. Receveing message form the client
// 3. Sending response to client
// 4. Receving resposne from a client
// Also we are using protocol messages and protocol responses to have a oreder and for easier communication

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "../../include/network.h"
#include "../../include/protocol.h"

// Send a ProtocolMessage to the client
int sendMessage(int sock, ProtocolMessage *msg)
{
    // Send the whole structure as raw bytes
    // This makes communication simple and predictable
    if (sendAll(sock, msg, sizeof(ProtocolMessage)) < 0) {
        perror("sendMessage");
        return -1;
    }

    return 0;
}

// Receive a ProtocolMessage from the client
int receiveMessage(int sock, ProtocolMessage *msg)
{
    // Read the fixed-size structure
    if (recvAll(sock, msg, sizeof(ProtocolMessage)) < 0) {
        perror("receiveMessage");
        return -1;
    }

    return 0;
}

// Send a ProtocolResponse to the client
int sendResponse(int sock, ProtocolResponse *res)
{
    if (sendAll(sock, res, sizeof(ProtocolResponse)) < 0) {
        perror("sendResponse");
        return -1;
    }

    return 0;
}

// Receive a ProtocolResponse from the client
int receiveResponse(int sock, ProtocolResponse *res)
{
    if (recvAll(sock, res, sizeof(ProtocolResponse)) < 0) {
        perror("receiveResponse");
        return -1;
    }

    return 0;
}
