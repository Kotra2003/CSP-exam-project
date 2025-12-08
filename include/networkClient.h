#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

// Connect to server and return socket FD, or -1 on error.
int connectToServer(const char *ip, int port);

// Send exactly 'size' bytes (TCP may send less per call).
// Returns 0 on success, -1 on error.
int sendAll(int sock, const void *buffer, int size);

// Receive exactly 'size' bytes.
// Returns 0 on success, -1 on error.
int recvAll(int sock, void *buffer, int size);

#endif
