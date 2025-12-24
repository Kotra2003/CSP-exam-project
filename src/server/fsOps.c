#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>


#include "../../include/fsOps.h"
#include "../../include/utils.h"

// ============================================================
// LOCKING IMPLEMENTATION — fcntl()
// ============================================================

int lockFileRead(int fd)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_RDLCK;   // shared lock
    fl.l_whence = SEEK_SET;

    return fcntl(fd, F_SETLKW, &fl);   // block until lock acquired
}

int lockFileWrite(int fd)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_WRLCK;   // exclusive lock
    fl.l_whence = SEEK_SET;

    return fcntl(fd, F_SETLKW, &fl);   // block until lock acquired
}

int unlockFile(int fd)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;

    return fcntl(fd, F_SETLK, &fl);
}

// ============================================================
// resolvePath (originalni kod – netaknut)
// ============================================================

int resolvePath(Session *s, const char *inputPath, char *outputPath)
{
    if (!s || !inputPath || !outputPath) return -1;

    char curRel[PATH_SIZE] = "";
    size_t homeLen = strlen(s->homeDir);

    if (strncmp(s->currentDir, s->homeDir, homeLen) == 0)
    {
        if (s->currentDir[homeLen] == '\0')
            curRel[0] = '\0';
        else if (s->currentDir[homeLen] == '/')
            strncpy(curRel, s->currentDir + homeLen + 1, PATH_SIZE);
        else
            strncpy(curRel, s->currentDir, PATH_SIZE);
    }

    char vpath[PATH_SIZE];

    if (inputPath[0] == '\0' || strcmp(inputPath, ".") == 0)
        snprintf(vpath, PATH_SIZE, "%s", curRel);
    else if (inputPath[0] == '/')
        snprintf(vpath, PATH_SIZE, "%s", inputPath + 1);
    else
    {
        if (curRel[0] == '\0')
            snprintf(vpath, PATH_SIZE, "%s", inputPath);
        else
            snprintf(vpath, PATH_SIZE, "%s/%s", curRel, inputPath);
    }

    char work[PATH_SIZE];
    strncpy(work, vpath, PATH_SIZE);

    char *parts[256];
    int count = 0;

    char *tok = strtok(work, "/");
    while (tok)
    {
        if (strcmp(tok, ".") == 0)
        {
        }
        else if (strcmp(tok, "..") == 0)
        {
            if (count > 0) count--;
        }
        else
            parts[count++] = tok;

        tok = strtok(NULL, "/");
    }

    char relNorm[PATH_SIZE];
    relNorm[0] = '\0';

    for (int i = 0; i < count; i++)
    {
        if (i > 0) strncat(relNorm, "/", PATH_SIZE - strlen(relNorm) - 1);
        strncat(relNorm, parts[i], PATH_SIZE - strlen(relNorm) - 1);
    }

    if (relNorm[0] == '\0')
        snprintf(outputPath, PATH_SIZE, "%s", s->homeDir);
    else
        snprintf(outputPath, PATH_SIZE, "%s/%s", s->homeDir, relNorm);

    return 0;
}

// ============================================================

int isInsideRoot(const char *rootDir, const char *fullPath)
{
    int len = strlen(rootDir);

    if (strncmp(rootDir, fullPath, len) != 0)
        return 0;

    return (fullPath[len] == '\0' || fullPath[len] == '/');
}

// ============================================================
// CREATE
// ============================================================

int fsCreate(const char *path, int permissions, int isDirectory)
{
    if (isDirectory)
    {
        if (mkdir(path, permissions) < 0) return -1;
    }
    else
    {
        int fd = open(path, O_CREAT | O_EXCL, permissions);
        if (fd < 0) return -1;
        close(fd);
    }
    return 0;
}

// ============================================================
// CHMOD
// ============================================================

int fsChmod(const char *path, int permissions)
{
    return chmod(path, permissions);
}

// ============================================================
// MOVE / RENAME
// ============================================================

int fsMove(const char *src, const char *dst)
{
    return rename(src, dst);
}

// ============================================================
// READ — Shared lock
// ============================================================

int fsReadFile(const char *path, char *buffer, int size, int offset)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    if (lockFileRead(fd) < 0) { close(fd); return -1; }

    if (lseek(fd, offset, SEEK_SET) < 0)
    {
        unlockFile(fd);
        close(fd);
        return -1;
    }

    int r = read(fd, buffer, size);

    unlockFile(fd);
    close(fd);

    return r;
}

// ============================================================
// WRITE — Exclusive lock
// ============================================================

int fsWriteFile(const char *path, const char *data, int size, int offset)
{
    int fd;

    /*
     * Pokušaj ATOMIČKI da kreiraš fajl.
     * Ako uspije → fajl je NOV i mi smo ga napravili.
     */
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0700);

    if (fd >= 0) {
        /* Fajl je nov → ispravi permisije zbog umask */
        if (fchmod(fd, 0700) < 0) {
            close(fd);
            return -1;
        }
    } else {
        /* Ako već postoji → samo ga otvori */
        if (errno != EEXIST)
            return -1;

        fd = open(path, O_WRONLY);
        if (fd < 0)
            return -1;
    }

    /* Ekskluzivni lock */
    if (lockFileWrite(fd) < 0) {
        close(fd);
        return -1;
    }

    /*
     * Ako je offset == 0:
     *  - overwrite (truncate)
     */
    if (offset == 0) {
        if (ftruncate(fd, 0) < 0) {
            unlockFile(fd);
            close(fd);
            return -1;
        }
    }

    /* Pomjeri se na offset */
    if (lseek(fd, offset, SEEK_SET) < 0) {
        unlockFile(fd);
        close(fd);
        return -1;
    }

    /* Upis podataka */
    int written = 0;
    if (size > 0) {
        written = write(fd, data, size);
        if (written < 0) {
            unlockFile(fd);
            close(fd);
            return -1;
        }
    }

    unlockFile(fd);
    close(fd);

    return written;
}

