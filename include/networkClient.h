#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

// Connect to server and return socket FD
int connectToServer(const char *ip, int port);

// Send exactly size bytes (TCP may send less)
int sendAll(int sock, const void *buffer, int size);

// Receive exactly size bytes
int recvAll(int sock, void *buffer, int size);

#endif
