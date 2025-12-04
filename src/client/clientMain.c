#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../include/network.h"
#include "../../include/protocol.h"

int main(int argc, char *argv[])
{
    // Check arguments
    if (argc < 3) {
        printf("Usage: %s <IP> <port>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);   // We need a number so we do ASCII to integer

    // Connect to server
    int sock = connectToServer(ip, port);
    if (sock < 0) {
        printf("Failed to connect to server.\n");
        return 1;
    }

    printf("Connected to server %s:%d\n", ip, port);

    // Buffer for client input
    char input[256];

    while (1) {
        printf("> ");
        fflush(stdout);     // Fast buffer clean 

        // Read user input
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("Input error.\n");
            break;
        }

        // Remove newline
        input[strcspn(input, "\n")] = 0;    // We want to delte \n form input, which we got from fgets once we pressed enter

        // Prepare protocol message
        ProtocolMessage msg;
        memset(&msg, 0, sizeof(msg));

        // Simple placeholder parsing
        // We can't imediatly close socket! We need to send command to server so it also closes clients socket on servers side
        if (strcmp(input, "exit") == 0) {
            msg.command = CMD_EXIT;
        } else {
            printf("Unknown command (for now only 'exit' works).\n");
            continue;
        }

        // Send message to server
        if (sendMessage(sock, &msg) < 0) {
            printf("Connection lost.\n");
            break;
        }

        // Receive server response
        ProtocolResponse res;
        if (receiveResponse(sock, &res) < 0) {
            printf("Server closed connection.\n");
            break;
        }

        // Print server response
        if (res.status == STATUS_OK) {
            printf("OK\n");
        } else {
            printf("ERROR\n");
        }

        // If exit, break the loop
        if (msg.command == CMD_EXIT) {
            break;
        }
    }

    close(sock);
    return 0;
}
