#include <comp421/filesystem.h>
#include <comp421/yalnix.h>
#include <comp421/iolib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "yfs_header.h"

struct info_file{
    int inode;
    int pos;
    int fd;
    int open_close; // open = 1, close = 0;
};
struct info_file file_info_collection[MAX_OPEN_FILES];
int lowest_fd;


/** global array of structs that represents the current file locations */

int find_lowest_fd(){

    //if(file_info_collection) return 0;

    return -1;
}

int Open(char *pathname) {
    struct my_msg test_message = {.type = OPEN_M, .data1 = strlen(pathname), .ptr = (void *) pathname};
    Send((void *) &test_message, -FILE_SERVER);

    //update the file descriptor.
    // message is overwritten
    int curr_inode = test_message.data1; //inode of the current open file
    int pos = 0;
    

    int lowest_fd;
    if( (lowest_fd = find_lowest_fd()) == -1){
        TracePrintf(0, "Unable to create more files than file descriptors");
        return -1;
    }
    //chcek the entry, and create the struct at the least/lowest file descriptor num
  
    //update bookkeeping
   // file_info_collection[lowest_fd] = malloc( sizeof(struct info_file) );
    
    file_info_collection[lowest_fd].inode = curr_inode;
    file_info_collection[lowest_fd].pos = pos;
    file_info_collection[lowest_fd].fd = lowest_fd;
    //file_info_collection[lowest_fd] = {.inode = curr_inode, .pos = pos, .fd = lowest_fd};
    file_info_collection[lowest_fd].open_close = 1;
    

    return 0;
}

int Close(int fd) {
    int curr_inode = file_info_collection[fd].inode;
    /** data1: inode number server*/
    struct my_msg test_message = {.type = CLOSE_M, .data1 = curr_inode};
    
    Send((void *) &test_message, -FILE_SERVER);

    

    TracePrintf(0, "In close\n");
    return 0;
}

int Create(char *pathname) {
    TracePrintf(0, "In create\n");
    struct my_msg test_message = {.type = CREATE_M, .data1 = strlen(pathname), .ptr = (void *) pathname};
    Send((void *) &test_message, -FILE_SERVER);
    
    //TODO update open or closed? 

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
    TracePrintf(0, "In mkdir\n");
    struct my_msg test_message = {.type = MKDIR_M, .data1 = strlen(pathname), .ptr = (void *) pathname};
    Send((void *) &test_message, -FILE_SERVER);
    
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
    struct my_msg test_message = {.type = SHUTDOWN_M};
    Send((void *) &test_message, -FILE_SERVER);
    return 0;
}