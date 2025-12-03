#ifndef NETWORK_H
#define NETWORK_H

// Simple network constant
#define MAX_BUFFER 4096

// Create a listening server socket
// This function creates a socket, binds it, and sets it to listen mode.
// Returns the socket file descriptor or -1 on error.
int createServerSocket(const char *ip, int port);

// Accept a new client connection
// Returns the client file descriptor or -1 on error.
int acceptClient(int serverFd);

// Connect to the server (client side)
// Returns the connected socket file descriptor or -1 on error.
int connectToServer(const char *ip, int port);

// Send all bytes reliably
// This function sends all data, retrying until everything is sent.
// Returns 0 on success, -1 on error.
int sendAll(int sock, const void *buffer, int size);

// Receive exactly "size" bytes
// Returns 0 on success, -1 on error.
int recvAll(int sock, void *buffer, int size);

#endif
