#ifndef CONCURRENCY_H
#define CONCURRENCY_H

#include "session.h"

#define MAX_LOCKS 128

typedef struct {
    char path[PATH_SIZE];   // path fajla/direktorijuma koji zaključavamo
    int fd;                 // file descriptor koji drži fcntl lock
    int locked;             // 1 ako je lock aktivan u ovom procesu
} FileLock;

// Inicijalizacija tabele lock-ova (nije obavezno ako je .bss = 0, ali držimo za svaki slučaj)
void initLocks();

// Ekskluzivni lock za dati path (blokira dok ne postane slobodan).
// Vraća 0 na uspjeh, -1 na grešku.
int acquireFileLock(const char *path);

// Oslobađa lock za dati path (ako postoji).
void releaseFileLock(const char *path);

#endif
