#ifndef NETWORK_H
#define NETWORK_H

#define MAX_BUFFER 4096   // buffer size for network I/O

// Server-side socket helpers
int createServerSocket(const char *ip, int port);
int acceptClient(int serverFd);

// Client-side connection
int connectToServer(const char *ip, int port);

// Send and receive fixed amount of data
int sendAll(int sock, const void *buffer, int size);
int recvAll(int sock, void *buffer, int size);

#endif
