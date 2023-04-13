#include <comp421/filesystem.h>
#include <comp421/yalnix.h>
#include <comp421/iolib.h>

struct my_msg {
    int data1;
    int data2;
    char data3[16];
    void *ptr;
};

int Open(char *pathname) {
    (void) pathname;
    TracePrintf(0, "In open\n");
    return 0;
}

int Close(int fd) {
    (void) fd;
    TracePrintf(0, "In close\n");
    return 0;
}

int Create(char *pathname) {
    (void) pathname;
    TracePrintf(0, "%d Size", sizeof(struct my_msg));
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
