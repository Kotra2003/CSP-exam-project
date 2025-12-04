#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../../include/fsOps.h"

// Build a full resolved path based on session and client input
int resolvePath(Session *s, const char *inputPath, char *outputPath)
{
    // For now: simple join with currentDir
    // Later we add real path sanitization
    if (inputPath[0] == '/') {
        snprintf(outputPath, PATH_SIZE, "%s", inputPath);
    } else {
        snprintf(outputPath, PATH_SIZE, "%s/%s", s->currentDir, inputPath);
    }

    return 0;
}

// Check if fullPath is inside rootDir
int isInsideRoot(const char *rootDir, const char *fullPath)
{
    // Placeholder: real logic later
    // For now always return inside
    return 1;
}

// Create a file or directory
int fsCreate(const char *path, int permissions, int isDirectory)
{
    if (isDirectory) {
        // Create directory
        if (mkdir(path, permissions) < 0) {
            perror("mkdir");
            return -1;
        }
    } else {
        // Create empty file (We use O_EXCL so we don't overwrite exesting file)
        int fd = open(path, O_CREAT | O_EXCL, permissions);
        if (fd < 0) {
            perror("open");
            return -1;
        }
        close(fd);
    }

    return 0;
}

// Change permissions of a file or directory
int fsChmod(const char *path, int permissions)
{
    if (chmod(path, permissions) < 0) {
        perror("chmod");
        return -1;
    }
    return 0;
}

// Move a file (Also used to rename a file)
int fsMove(const char *src, const char *dst)
{
    if (rename(src, dst) < 0) {
        perror("rename");
        return -1;
    }
    return 0;
}

// Delete a file or directory
int fsDelete(const char *path)
{
    if (unlink(path) < 0) {
        perror("unlink");
        return -1;
    }
    return 0;
}

// Read file content into buffer
int fsReadFile(const char *path, char *buffer, int size, int offset)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    // Move to offset (We use SEEK_SET to put measure offset form starting position)
    if (lseek(fd, offset, SEEK_SET) < 0) {
        perror("lseek");
        close(fd);
        return -1;
    }

    int readBytes = read(fd, buffer, size);
    if (readBytes < 0) {
        perror("read");
        close(fd);
        return -1;
    }

    close(fd);
    return readBytes;
}

// Write data into a file
int fsWriteFile(const char *path, const char *data, int size, int offset)
{
    int fd = open(path, O_WRONLY | O_CREAT, 0700);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    // Move to offset
    if (lseek(fd, offset, SEEK_SET) < 0) {
        perror("lseek");
        close(fd);
        return -1;
    }

    int written = write(fd, data, size);
    if (written < 0) {
        perror("write");
        close(fd);
        return -1;
    }

    close(fd);
    return written;
}
