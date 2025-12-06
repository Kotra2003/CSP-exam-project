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
    char temp[PATH_SIZE];   // Where we are going to store the path

    // Case 1: absolute path
    if (inputPath[0] == '/') {
        strncpy(temp, inputPath, PATH_SIZE);
    } 
    else {
        // Relative path → base is currentDir
        snprintf(temp, PATH_SIZE, "%s/%s", s->currentDir, inputPath);
    }

    // We want to get rid of . and ..
    char normalized[PATH_SIZE]; // This is going to represent a used path
    char *tokens[256];  // We need pointers to cut diff parts of path
    int count = 0;  // How many parts of path we are going to have

    char *p = strtok(temp, "/");    // Cutting path by / (in the end we just get parts like names of directives and also .. or .)
    while (p != NULL) {
        if (strcmp(p, ".") == 0) {

        }
        else if (strcmp(p, "..") == 0) {
            if (count > 0) count--;     // We can't skip it because it represents specific movement
        }
        else {
            tokens[count++] = p;
        }
        p = strtok(NULL, "/");  // We want to start from the part where we have ended last time
    }

    // Reuilding normalized path (Just a path without . or ..)
    strcpy(normalized, "");
    for (int i = 0; i < count; i++) {
        strcat(normalized, "/");
        strcat(normalized, tokens[i]);  
    }

    // If nothing inside → "/" (just in case)
    if (count == 0)
        strcpy(normalized, "/");

    strncpy(outputPath, normalized, PATH_SIZE);
    return 0;
}


// Check if fullPath is inside rootDir
int isInsideRoot(const char *rootDir, const char *fullPath)
{
    int len = strlen(rootDir);

    // Special case rootDir ending with '/'
    if (rootDir[len - 1] == '/')
        return strncmp(rootDir, fullPath, len) == 0;

    // We need to be sure that path where we want to go starts with rootDir
    if (strncmp(rootDir, fullPath, len) != 0)
        return 0;

    // We need to be sure that path ends with / or \0 to be sure that for example rootDir/something../etc isn't possibile
    if (fullPath[len] == '\0' || fullPath[len] == '/')
        return 1;

    return 0;
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
