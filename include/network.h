#ifndef NETWORK_H
#define NETWORK_H

#define MAX_BUFFER 4096   // Generic buffer size for network I/O

// Server-side networking helpers.
// Used to create and manage the listening socket.
int createServerSocket(const char *ip, int port);
int acceptClient(int serverFd);

// Client-side connection helper.
// Establishes a TCP connection to the server.
int connectToServer(const char *ip, int port);

// Reliable TCP send/receive helpers.
// Ensure that the exact number of bytes is transmitted,
// handling partial send() and recv() calls.
int sendAll(int sock, const void *buffer, int size);
int recvAll(int sock, void *buffer, int size);

#endif
