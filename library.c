#include <comp421/filesystem.h>
#include <comp421/yalnix.h>
#include <comp421/iolib.h>
#include <string.h>

#include "yfs_header.h"


int[MAX_OPEN_FILES] file_info_collection;

struct info_file{
    int inode;
    int pos;
    int fd;
    bool openClose;
}
/** global array of structs that represents the current file locations */


int Open(char *pathname) {
    struct my_msg test_message = {.type = OPEN_M, .data1 = strlen(pathname), .ptr = (void *) pathname};
    Send((void *) &test_message, -FILE_SERVER);

    

    return 0;
}

int Close(int fd) {
    (void) fd;
    TracePrintf(0, "In close\n");
    return 0;
}

int Create(char *pathname) {
    struct my_msg test_message = {.type = CREATE_M, .data1 = strlen(pathname), .ptr = (void *) pathname};
    Send((void *) &test_message, -FILE_SERVER);
    TracePrintf(0, "In create\n");
    return 0;
}

int Read(int fd, void *buf, int size) {
    (void) fd;
    (void) buf;
    (void) size;
    TracePrintf(0, "in read\n");
    return 0;
}

int Write(int fd, void *buf, int size) {
    (void) fd;
    (void) buf;
    (void) size;
    TracePrintf(0, "in write\n");
    return 0;
}

int Seek(int fd, int offset, int whence) {
    (void) fd;
    (void) offset;
    (void) whence;
    return 0;
}

int Link(char *oldname, char *newname) {
    (void) oldname;
    (void) newname;
    return 0;
}

int Unlink(char *pathname) {
    (void) pathname;
    return 0;
}

int SymLink(char *oldname, char *newname) {
    (void) oldname;
    (void) newname;
    return 0;
}

int ReadLink(char *pathname, char *buf, int len) {
    (void) pathname;
    (void) buf;
    (void) len;
    return 0;
}

int MkDir(char *pathname) {
    (void) pathname;
    return 0;
}

int RmDir(char *pathname) {
    (void) pathname;
    return 0;
}

int ChDir(char *pathname) {
    (void) pathname;
    return 0;
}

int Stat(char *pathname, struct Stat *statbuf) {
    (void) pathname;
    (void) statbuf;
    return 0;
}

int Sync(void) {
    return 0;
}

int Shutdown(void) {
    return 0;
}
