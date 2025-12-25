#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>

#include "../../include/fsOps.h"
#include "../../include/utils.h"

extern const char *gRootDir;

// ============================================================
// LOCKING IMPLEMENTATION — fcntl()
// ============================================================

int lockFileRead(int fd)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_RDLCK;   // shared lock
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;         // whole file

    return fcntl(fd, F_SETLKW, &fl);   // block until lock acquired
}

int lockFileWrite(int fd)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_WRLCK;   // exclusive lock
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;         // whole file

    return fcntl(fd, F_SETLKW, &fl);   // block until lock acquired
}

int unlockFile(int fd)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;

    return fcntl(fd, F_SETLK, &fl);
}

// ============================================================
// POMOĆNE FUNKCIJE ZA PATH HANDLING
// ============================================================

// Normaliziraj path - ukloni /./, /../, višestruke //
static char* normalize_path(const char *path, char *normalized, size_t size) {
    if (!path || !normalized || size == 0) return NULL;
    
    char tmp[PATH_SIZE];
    strncpy(tmp, path, sizeof(tmp));
    tmp[sizeof(tmp)-1] = '\0';
    
    char *components[256];
    int count = 0;
    
    // Rastavi na komponente
    char *token = strtok(tmp, "/");
    while (token != NULL && count < 256) {
        if (strcmp(token, ".") == 0) {
            // ignoriraj
        } else if (strcmp(token, "..") == 0) {
            if (count > 0) count--;
        } else {
            components[count++] = token;
        }
        token = strtok(NULL, "/");
    }
    
    // Sastavi normalizirani path
    normalized[0] = '\0';
    if (path[0] == '/') {
        strcat(normalized, "/");
    }
    
    for (int i = 0; i < count; i++) {
        if (i > 0) strcat(normalized, "/");
        strcat(normalized, components[i]);
    }
    
    if (count == 0 && path[0] == '/') {
        strcpy(normalized, "/");
    }
    
    return normalized;
}

// ============================================================
// NOVA IMPLEMENTACIJA resolvePath
// ============================================================
int resolvePath(Session *s, const char *inputPath, char *outputPath)
{
    if (!s || !inputPath || !outputPath) {
        return -1;
    }
    
    char normalized[PATH_SIZE];
    char result[PATH_SIZE];
    result[0] = '\0';
    
    // 1) Odredi bazu za razrješavanje
    char base[PATH_SIZE];
    if (inputPath[0] == '/') {
        // Apsolutna putanja - počinje od korijena (ali ne root dir servera, nego korijena unutar root dir-a)
        if (s->isLoggedIn) {
            // Ako je logovan, / znači njegov home
            snprintf(base, PATH_SIZE, "%s", s->homeDir);
        } else {
            // Nije logovan - ne bi trebalo doći ovdje
            snprintf(base, PATH_SIZE, "%s", gRootDir);
        }
    } else {
        // Relativna putanja - počinje od trenutnog direktorija
        snprintf(base, PATH_SIZE, "%s", s->currentDir);
    }
    
    // 2) Konstruiši punu putanju
    if (strcmp(inputPath, "/") == 0 || strcmp(inputPath, "") == 0) {
        // Root ili prazno - vraća bazu
        strncpy(result, base, PATH_SIZE - 1);
        result[PATH_SIZE - 1] = '\0';
    } else if (inputPath[0] == '/') {
        // Apsolutna putanja (unutar home ili root)
        if (s->isLoggedIn) {
            // /drugi_user/file -> gRootDir/drugi_user/file
            snprintf(result, PATH_SIZE, "%s%s", gRootDir, inputPath);
        } else {
            snprintf(result, PATH_SIZE, "%s%s", gRootDir, inputPath);
        }
    } else {
        // Relativna putanja - sigurno kopiranje
        size_t base_len = strlen(base);
        size_t input_len = strlen(inputPath);
        
        if (base_len + 1 + input_len + 1 > PATH_SIZE) {
            return -1; // Predugačka putanja
        }
        
        strcpy(result, base);
        if (base[base_len - 1] != '/') {
            strcat(result, "/");
        }
        strcat(result, inputPath);
    }
    
    // 3) Normaliziraj putanju (obrađi . i ..)
    normalize_path(result, normalized, PATH_SIZE);
    
    // 4) Provjeri da li je unutar root direktorija servera
    if (strncmp(normalized, gRootDir, strlen(gRootDir)) != 0) {
        return -1; // Izvan root-a
    }
    
    // 5) Kopiraj rezultat u output
    strncpy(outputPath, normalized, PATH_SIZE);
    outputPath[PATH_SIZE - 1] = '\0';
    
    return 0;
}

// ============================================================
// NOVA IMPLEMENTACIJA isInsideRoot
// ============================================================
int isInsideRoot(const char *rootDir, const char *fullPath)
{
    if (!rootDir || !fullPath) return 0;
    
    size_t rootLen = strlen(rootDir);
    
    // Provjeri da putanja počinje sa rootDir
    if (strncmp(rootDir, fullPath, rootLen) != 0) {
        return 0;
    }
    
    // Provjeri da je nakon rootDir ili '/'
    if (fullPath[rootLen] == '\0') {
        return 1; // Točno root dir
    }
    
    if (fullPath[rootLen] == '/') {
        return 1; // Pod-direktorij unutar root
    }
    
    return 0; // Nešto drugo (npr. rootDir_something)
}

// ============================================================
// NOVA FUNKCIJA: Provjeri da li je putanja unutar user home
// ============================================================
int isInsideHome(const char *homeDir, const char *fullPath)
{
    if (!homeDir || !fullPath) return 0;
    
    size_t homeLen = strlen(homeDir);
    
    // Provjeri da putanja počinje sa homeDir
    if (strncmp(homeDir, fullPath, homeLen) != 0) {
        return 0;
    }
    
    // Provjeri da je nakon homeDir ili '/'
    if (fullPath[homeLen] == '\0') {
        return 1; // Točno home dir
    }
    
    if (fullPath[homeLen] == '/') {
        return 1; // Pod-direktorij unutar home
    }
    
    return 0; // Nešto drugo
}

// ============================================================
// CREATE
// ============================================================
int fsCreate(const char *path, int permissions, int isDirectory)
{
    if (isDirectory) {
        if (mkdir(path, permissions) < 0) return -1;
    } else {
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

    if (lockFileRead(fd) < 0) { 
        close(fd); 
        return -1; 
    }

    if (lseek(fd, offset, SEEK_SET) < 0) {
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
