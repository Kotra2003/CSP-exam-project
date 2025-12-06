#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../include/protocol.h"
#include "../../include/utils.h"
#include "../../include/networkClient.h"
#include "../../include/clientCommands.h"

int main()
{
    int sock = connectToServer("127.0.0.1", 8080);
    if (sock < 0) {
        printf("Could not connect to server.\n");
        return 1;
    }

    printf("Connected to server.\n");

    char input[512];

    while (1)
    {
        printf("> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin))
            break;

        // Remove newline
        input[strcspn(input, "\n")] = '\0';

        // ---------------------------------------------------------
        //                      UPLOAD
        // ---------------------------------------------------------
        if (strncmp(input, "upload ", 7) == 0) {
            char local[256], remote[256];
            if (sscanf(input + 7, "%s %s", local, remote) == 2) {
                clientUpload(sock, local, remote);
            } else {
                printf("Usage: upload <local> <remote>\n");
            }
            continue;
        }

        // ---------------------------------------------------------
        //                      DOWNLOAD
        // ---------------------------------------------------------
        if (strncmp(input, "download ", 9) == 0) {
            char remote[256], local[256];
            if (sscanf(input + 9, "%s %s", remote, local) == 2) {
                clientDownload(sock, remote, local);
            } else {
                printf("Usage: download <remote> <local>\n");
            }
            continue;
        }

        // ---------------------------------------------------------
        //        ALL OTHER SIMPLE COMMANDS (login, list, cd…)
        // ---------------------------------------------------------
        ProtocolMessage msg;
        memset(&msg, 0, sizeof(msg));

        // Parse: command arg1 arg2 arg3
        // Example: 3 folder 755 dir
        sscanf(input, "%d %s %s %s",
               &msg.command,
               msg.arg1, msg.arg2, msg.arg3);

        int status = clientSendSimple(sock, &msg);

        if (msg.command == CMD_EXIT)
            break;
    }

    close(sock);
    return 0;
}
