#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../include/serverCommands.h"
#include "../../include/protocol.h"
#include "../../include/session.h"

// Decide which command has been received
int processCommand(int clientFd, ProtocolMessage *msg, Session *session)
{
    switch (msg->command) {

        case CMD_LOGIN:
            return handleLogin(clientFd, msg, session);

        case CMD_CREATE_USER:
            return handleCreateUser(clientFd, msg, session);

        case CMD_CREATE:
            return handleCreate(clientFd, msg, session);

        case CMD_CHMOD:
            return handleChmod(clientFd, msg, session);

        case CMD_MOVE:
            return handleMove(clientFd, msg, session);

        case CMD_CD:
            return handleCd(clientFd, msg, session);

        case CMD_LIST:
            return handleList(clientFd, msg, session);

        case CMD_READ:
            return handleRead(clientFd, msg, session);

        case CMD_WRITE:
            return handleWrite(clientFd, msg, session);

        case CMD_DELETE:
            return handleDelete(clientFd, msg, session);

        case CMD_EXIT:
            // return 1 so serverMain knows to close the connection
            return 1;

        default:
            printf("Unknown command received.\n");
            return 0;
    }
}

// ------------------- Individual command skeletons ---------------------

int handleLogin(int clientFd, ProtocolMessage *msg, Session *session)
{
    // Placeholder: real logic later
    printf("Login command received.\n");
    return 0;
}

int handleCreateUser(int clientFd, ProtocolMessage *msg, Session *session)
{
    printf("Create user command received.\n");
    return 0;
}

int handleCreate(int clientFd, ProtocolMessage *msg, Session *session)
{
    printf("Create command received.\n");
    return 0;
}

int handleChmod(int clientFd, ProtocolMessage *msg, Session *session)
{
    printf("Chmod command received.\n");
    return 0;
}

int handleMove(int clientFd, ProtocolMessage *msg, Session *session)
{
    printf("Move command received.\n");
    return 0;
}

int handleCd(int clientFd, ProtocolMessage *msg, Session *session)
{
    printf("Cd command received.\n");
    return 0;
}

int handleList(int clientFd, ProtocolMessage *msg, Session *session)
{
    printf("List command received.\n");
    return 0;
}

int handleRead(int clientFd, ProtocolMessage *msg, Session *session)
{
    printf("Read command received.\n");
    return 0;
}

int handleWrite(int clientFd, ProtocolMessage *msg, Session *session)
{
    printf("Write command received.\n");
    return 0;
}

int handleDelete(int clientFd, ProtocolMessage *msg, Session *session)
{
    printf("Delete command received.\n");
    return 0;
}
