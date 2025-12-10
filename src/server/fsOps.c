#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../../include/fsOps.h"
#include "../../include/utils.h"

// =====================================================================
// resolvePath
//
// Cilj: od korisničke putanje (relative ili "apsolutne" /foo)
// dobiti *stvarnu* putanju unutar korisničkog sandboxa:
//
//   - sandbox root = session->homeDir      (npr. "rootDir/Aleksa")
//   - session->currentDir je uvijek unutar homeDir
//   - ".." nikad ne može izaći iz homeDir
//   - "/something" tretiramo kao "from homeDir"
// =====================================================================
int resolvePath(Session *s, const char *inputPath, char *outputPath)
{
    if (!s || !inputPath || !outputPath) {
        return -1;
    }

    // 1) Izračunaj trenutni relativni path u odnosu na homeDir
    //    homeDir:      rootDir/Aleksa
    //    currentDir:   rootDir/Aleksa/docs/sub
    //    => curRel:    "docs/sub"
    char curRel[PATH_SIZE] = "";
    size_t homeLen = strlen(s->homeDir);

    if (strncmp(s->currentDir, s->homeDir, homeLen) == 0) {
        if (s->currentDir[homeLen] == '\0') {
            // currentDir == homeDir -> curRel = ""
            curRel[0] = '\0';
        } else if (s->currentDir[homeLen] == '/') {
            // nešto kao "homeDir/..."
            strncpy(curRel, s->currentDir + homeLen + 1, PATH_SIZE);
            curRel[PATH_SIZE - 1] = '\0';
        } else {
            // teorijski ne bi smjelo da se desi, ali fallback:
            strncpy(curRel, s->currentDir, PATH_SIZE);
            curRel[PATH_SIZE - 1] = '\0';
        }
    } else {
        // još jedan fallback: ako currentDir ne krene sa homeDir,
        // uzmi ga kao "relativan" (ovako bar ne eksplodiramo).
        strncpy(curRel, s->currentDir, PATH_SIZE);
        curRel[PATH_SIZE - 1] = '\0';
    }

    // 2) Napravi "virtualni path" (relativan u odnosu na homeDir)
    //    koji ćemo normalizovati: , uklanjanje . i ..
    char vpath[PATH_SIZE];

    if (inputPath[0] == '\0' || strcmp(inputPath, ".") == 0) {
        // prazno -> ostajemo gdje smo (curRel)
        strncpy(vpath, curRel, PATH_SIZE);
        vpath[PATH_SIZE - 1] = '\0';
    }
    else if (inputPath[0] == '/') {
        // "/foo/bar" znači "od homeDir-a", tj. ignoriramo curRel
        // i koristimo sve POSLIJE početnog '/'
        snprintf(vpath, PATH_SIZE, "%s", inputPath + 1);
    }
    else {
        // relativan path prema currentDir-u unutar homeDir-a
        if (curRel[0] == '\0') {
            // trenutno smo u homeDir → samo inputPath
            snprintf(vpath, PATH_SIZE, "%s", inputPath);
        } else {
            // curRel/inputPath
            snprintf(vpath, PATH_SIZE, "%s/%s", curRel, inputPath);
        }
    }

    // 3) Normalizacija: ukloni "." i obradi ".." bez izlaska iz korijena.
    //    Rezultat: relNorm (relativno u odnosu na homeDir)
    char work[PATH_SIZE];
    strncpy(work, vpath, PATH_SIZE);
    work[PATH_SIZE - 1] = '\0';

    char *parts[256];
    int count = 0;

    char *tok = strtok(work, "/");
    while (tok) {
        if (strcmp(tok, ".") == 0) {
            // ignoriši
        }
        else if (strcmp(tok, "..") == 0) {
            // idemo jedan nivo nazad, ali ne ispod root-a
            if (count > 0)
                count--;
        }
        else if (tok[0] != '\0') {
            parts[count++] = tok;
        }
        tok = strtok(NULL, "/");
    }

    char relNorm[PATH_SIZE];
    relNorm[0] = '\0';

    for (int i = 0; i < count; i++) {
        if (i > 0)
            strncat(relNorm, "/", PATH_SIZE - strlen(relNorm) - 1);

        strncat(relNorm, parts[i], PATH_SIZE - strlen(relNorm) - 1);
    }

    // 4) Sastavi finalnu punu putanju: homeDir + "/" + relNorm
    if (relNorm[0] == '\0') {
        // ostali smo u root-u korisnika
        strncpy(outputPath, s->homeDir, PATH_SIZE);
        outputPath[PATH_SIZE - 1] = '\0';
    } else {
        snprintf(outputPath, PATH_SIZE, "%s/%s", s->homeDir, relNorm);
    }

    return 0;
}

// =====================================================================
// Provera da li je fullPath unutar rootDir (string-prefix provjera)
//
// NAPOMENA: koriste je i za gRootDir i za session->homeDir.
//           Uz novi resolvePath, svi user pathovi AUTOMATSKI počinju
//           sa session->homeDir, koji počinje sa gRootDir.
// =====================================================================
int isInsideRoot(const char *rootDir, const char *fullPath)
{
    int len = strlen(rootDir);

    if (strncmp(rootDir, fullPath, len) != 0)
        return 0;

    // pun match (rootDir) ili rootDir/...
    if (fullPath[len] == '\0' || fullPath[len] == '/')
        return 1;

    return 0;
}

// =====================================================================
// Create file or directory
// =====================================================================
int fsCreate(const char *path, int permissions, int isDirectory)
{
    if (isDirectory) {
        if (mkdir(path, permissions) < 0) {
            perror("mkdir");
            return -1;
        }
    } else {
        int fd = open(path, O_CREAT | O_EXCL, permissions);
        if (fd < 0) {
            perror("open");
            return -1;
        }
        close(fd);
    }
    return 0;
}

// =====================================================================
// Change permissions
// =====================================================================
int fsChmod(const char *path, int permissions)
{
    if (chmod(path, permissions) < 0) {
        perror("chmod");
        return -1;
    }
    return 0;
}

// =====================================================================
// Move (rename)
// =====================================================================
int fsMove(const char *src, const char *dst)
{
    if (rename(src, dst) < 0) {
        perror("rename");
        return -1;
    }
    return 0;
}

// =====================================================================
// Delete file or directory (već je provjereno da je dir prazan ako je dir)
// =====================================================================
int fsDelete(const char *path)
{
    if (unlink(path) < 0) {
        perror("unlink");
        return -1;
    }
    return 0;
}

// =====================================================================
// Read file into buffer (offset + size)
// =====================================================================
int fsReadFile(const char *path, char *buffer, int size, int offset)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    if (lseek(fd, offset, SEEK_SET) < 0) {
        perror("lseek");
        close(fd);
        return -1;
    }

    int readBytes = (int)read(fd, buffer, size);
    if (readBytes < 0) {
        perror("read");
        close(fd);
        return -1;
    }

    close(fd);
    return readBytes;
}

// =====================================================================
// Write to file (kreira ako ne postoji, 0700)
// =====================================================================
int fsWriteFile(const char *path, const char *data, int size, int offset)
{
    int fd;

    if (offset == 0) {
        // overwrite: obriši stari sadržaj
        fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    } else {
        // piši od offseta, ne diraj ostatak
        fd = open(path, O_WRONLY | O_CREAT, 0700);
    }

    if (fd < 0) {
        perror("open");
        return -1;
    }

    if (lseek(fd, offset, SEEK_SET) < 0) {
        perror("lseek");
        close(fd);
        return -1;
    }

    int written = (int)write(fd, data, size);
    if (written < 0) {
        perror("write");
        close(fd);
        return -1;
    }

    close(fd);
    return written;
}

