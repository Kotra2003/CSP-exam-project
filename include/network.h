#ifndef NETWORK_H
#define NETWORK_H

// Maximum buffer used for generic I/O operations
#define MAX_BUFFER 4096

// Create a listening server socket.
// Returns the socket file descriptor, or -1 on error.
int createServerSocket(const char *ip, int port);

// Accept a new client connection on a listening socket.
// Returns the client file descriptor, or -1 on error.
int acceptClient(int serverFd);

// Connect to a server (client side).
// Returns the connected socket file descriptor, or -1 on error.
int connectToServer(const char *ip, int port);

// Send exactly 'size' bytes on the socket.
// Returns 0 on success, -1 on error.
int sendAll(int sock, const void *buffer, int size);

// Receive exactly 'size' bytes from the socket.
// Returns 0 on success, -1 on error.
int recvAll(int sock, void *buffer, int size);

#endif
