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
int current_inum = ROOTINODE;


/** global array of structs that represents the current file locations */

int find_lowest_fd(){

    //if(file_info_collection) return 0;

    //file descriptor begins with 0 --> MAX_OPEN_FILES goes from 0 to MAX_OPEN_FILES - 1

    int fd_index;
    for(fd_index = 0; fd_index < MAX_OPEN_FILES; fd_index++)
    {
        if(file_info_collection[fd_index].open_close == 0)
        {
            return fd_index;
        }
    }
    return -1;
}

int Open(char *pathname) {
    struct my_msg test_message = {.type = OPEN_M, .data1 = strlen(pathname), .data2 = current_inum, .ptr = (void *) pathname};
    Send((void *) &test_message, -FILE_SERVER);

    //update the file descriptor.
    // message is overwritten
    int curr_inode = test_message.data3; //inode of the current open file
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
    

    return lowest_fd;
}

int Close(int fd) {
    // int curr_inode = file_info_collection[fd].inode;
    /** data1: inode number server*/
    // struct my_msg test_message = {.type = CLOSE_M, .data1 = curr_inode};
    
    // Send((void *) &test_message, -FILE_SERVER);

    file_info_collection[fd].inode = 0;
    file_info_collection[fd].pos = 0;
    file_info_collection[fd].fd = lowest_fd;
    if (file_info_collection[fd].open_close == 0) {
        return ERROR;
    }
    file_info_collection[fd].open_close = 0;

    TracePrintf(0, "In close\n");
    return 0;
}

int Create(char *pathname) {
    TracePrintf(0, "In create\n");
    struct my_msg test_message = {.type = CREATE_M, .data1 = strlen(pathname), .data2 = current_inum, .ptr = (void *) pathname};
    Send((void *) &test_message, -FILE_SERVER);
    
    //TODO Need to open file
    int curr_inode = test_message.data3;
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

    return lowest_fd;
}

int Read(int fd, void *buf, int size) {
    TracePrintf(0, "IN read\n");
   
    struct my_msg test_message = {.type = READ_M, .data1 = file_info_collection[fd].inode, .data2 = size
    , .data3 = file_info_collection[fd].pos, .ptr = buf};
    Send((void *) &test_message, -FILE_SERVER);

    //inode_check = (int) message->data1;
    //number_to_read = (int) message->data2;
    //current_position = (int) message->data3;
    //buf_readTo = (void *) message->ptr;

    //update message
    //TODO


    TracePrintf(0, "in read\n");
    return 0;
}

int Write(int fd, void *buf, int size) {

    struct my_msg test_message = {.type = WRITE_M, .data1 = file_info_collection[fd].inode, .data2 = size
    , .data3 = file_info_collection[fd].pos, .ptr = buf};
    Send((void *) &test_message, -FILE_SERVER);

    //update TODO    

    TracePrintf(0, "in write\n");
    return 0;
}

int Seek(int fd, int offset, int whence) {
    TracePrintf(0, "in seek\n");
    int new_cur_pos;
    if (whence == SEEK_SET) {
        new_cur_pos = 0 + offset;
        file_info_collection[fd].pos = new_cur_pos;
    } else if (whence == SEEK_CUR) {
        new_cur_pos = file_info_collection[fd].pos + offset;
    } else if (whence == SEEK_END) {
        // Get size from node
        struct my_msg test_message = {.type = SEEK_M, .data2 = file_info_collection[fd].inode};
        Send((void *) &test_message, -FILE_SERVER);

        new_cur_pos = test_message.data3 + offset;

        // TODO: Handle when seek is beyond size
    } else {
        TracePrintf(0, "ERROR: Sent invalid whence\n");
        new_cur_pos = -1;
    }

    file_info_collection[fd].pos = new_cur_pos;
    return new_cur_pos;
}

int Link(char *oldname, char *newname) {
    TracePrintf(0, "In Link\n");
    struct my_msg test_message = {.type = LINK_M, .data1 = strlen(oldname), .data2 = current_inum, .data3 = strlen(newname), .ptr2 = (void *) newname, .ptr = (void *) oldname};
    Send((void *) &test_message, -FILE_SERVER);
    return test_message.data2;
}

int Unlink(char *pathname) {
    TracePrintf(0, "In unlink\n");
    struct my_msg test_message = {.type = UNLINK_M, .data1 = strlen(pathname), .data2 = current_inum, .ptr = (void *) pathname};
    Send((void *) &test_message, -FILE_SERVER);
    return test_message.data2;
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
    struct my_msg test_message = {.type = MKDIR_M, .data1 = strlen(pathname), .data2 = current_inum, .ptr = (void *) pathname};
    Send((void *) &test_message, -FILE_SERVER);
    
    return test_message.data2;
}

int RmDir(char *pathname) {
    TracePrintf(0, "In rmdir\n");
    struct my_msg test_message = {.type = RMDIR_M, .data1 = strlen(pathname), .data2 = current_inum, .ptr = (void *) pathname};
    Send((void *) &test_message, -FILE_SERVER);
    
    return test_message.data2;
}

int ChDir(char *pathname) {
    TracePrintf(0, "In ChDir\n");
    struct my_msg test_message = {.type = CHDIR_M, .data1 = strlen(pathname), .data2 = current_inum, .ptr = (void *) pathname};
    Send((void *) &test_message, -FILE_SERVER);

    // Overriden, make old current dir the new one
    current_inum = test_message.data3;
    TracePrintf(0, "Changed directory to this inode: %d\n", current_inum);

    return test_message.data2;
}

int Stat(char *pathname, struct Stat *statbuf) {
    TracePrintf(0, "In Stat\n");
    
    struct my_msg test_message = {.type = STAT_M, .data1 = strlen(pathname), .data2 = current_inum, .ptr2 = (void *) statbuf, .ptr = (void *) pathname};
    // add pointer to stat buf inside data3
    Send((void *) &test_message, -FILE_SERVER);

    return test_message.data2;
}

int Sync(void) {
    return 0;
}

int Shutdown(void) {
    struct my_msg test_message = {.type = SHUTDOWN_M};
    Send((void *) &test_message, -FILE_SERVER);
    return 0;
}
