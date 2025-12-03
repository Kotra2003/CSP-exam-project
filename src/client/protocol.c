// All function used are same as in server file
// And also as in server file we are using sendAll and recvAll to be compleate, as well as protocolMessages and protocolResponses

#include <stdio.h>
#include <unistd.h>

#include "../../include/network.h"
#include "../../include/protocol.h"

// Send a ProtocolMessage to the server
int sendMessage(int sock, ProtocolMessage *msg)
{
    // Send the whole structure as raw bytes
    // This makes communication simple and fixed-size
    if (sendAll(sock, msg, sizeof(ProtocolMessage)) < 0) {
        perror("sendMessage");
        return -1;
    }

    return 0;
}

// Receive a ProtocolMessage from the server
int receiveMessage(int sock, ProtocolMessage *msg)
{
    // Read the full structure from the socket
    if (recvAll(sock, msg, sizeof(ProtocolMessage)) < 0) {
        perror("receiveMessage");
        return -1;
    }

    return 0;
}

// Send a ProtocolResponse (It is not actualy used in program from clinet side but just in case)
int sendResponse(int sock, ProtocolResponse *res)
{
    if (sendAll(sock, res, sizeof(ProtocolResponse)) < 0) {
        perror("sendResponse");
        return -1;
    }

    return 0;
}

// Receive a ProtocolResponse from the server
int receiveResponse(int sock, ProtocolResponse *res)
{
    if (recvAll(sock, res, sizeof(ProtocolResponse)) < 0) {
        perror("receiveResponse");
        return -1;
    }

    return 0;
}
