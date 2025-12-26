#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

// Connect to the server and return a connected socket file descriptor.
// Used by the client and by background transfer processes.
int connectToServer(const char *ip, int port);

// Send exactly 'size' bytes over a TCP socket.
// Handles partial send() calls internally.
int sendAll(int sock, const void *buffer, int size);

// Receive exactly 'size' bytes from a TCP socket.
// Handles partial recv() calls internally.
int recvAll(int sock, void *buffer, int size);

#endif
