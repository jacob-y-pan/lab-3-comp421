#include <comp421/filesystem.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yfs_header.h"

int num_inodes;
int num_blocks;
int num_data_blocks;
// Global arrays because f stupid functions
int *free_inodes;
int *free_blocks;

// Store the current folder we're in (with inode)?
int current_inode_directory;

// Store the first block since will always stay constant
void *first_block;

// Holder for checking readSector and WriteSector errors
int c;

// Functions
int find_free_inode();
int find_free_block();
int check_folder(int curr_inum, char *curr_pathname, int parent_inum, int mode);
struct dir_entry create_file_dir(char *actual_filename, int file_dir, int parent_inum, int append);
int remove_inode(int inum);

// Helper Functions



int
main(int argc, char **argv)
{
    (void) argc;

    // Per page 19, register this server
    Register(FILE_SERVER);

    // Fork and exec the first user process
    if (Fork() == 0) {
        // As child, execute the child process (client)
        Exec(argv[1], argv + 1);
    } else {
        // ACTUAL PROCESS
        // Get first block
        TracePrintf(0, "Debug: Reading first block\n");
        first_block = malloc(BLOCKSIZE);
        if ((c = ReadSector(1, (void *) first_block)) == ERROR) {
            return ERROR;
        }
        // Get Fs_header
        struct fs_header *main_fs_header = (struct fs_header *) first_block;
        TracePrintf(0, "Fs_header values: num inodes: %d\n", main_fs_header->num_inodes);
        // Make array of free inodes - all init'd to 0
        num_inodes = main_fs_header->num_inodes;
        num_blocks = main_fs_header->num_blocks;
        
        // Make num_inodes + 1 because fs_header
        // 0 = free 1 = not free -1 = never can be free
        int arr_free_inodes[num_inodes + 1];
        // Mark 0th as not free (fs_header)
        arr_free_inodes[0] = -1;
        // Mark 1st as not free (root inode)
        arr_free_inodes[ROOTINODE] = ROOTINODE;
        // Scan all the other inodes to see if free
        struct inode *root_inode = (struct inode *) (first_block + sizeof(struct fs_header));
        // Get ready to get all the blocks needed for all the inodes - can infer from the first block of root
        int num_blocks_for_inodes = root_inode->direct[0] - 1;
        // In case we need it: start of the data blocks
        // int start_data_blocks = root_inode->direct[0];
        // Init free blocks - since concerned about only data blocks, just subtract those
        // 0 = free 1 = not free -1 = never can be free
        // free_blocks = (int *) malloc(sizeof(int) * (num_blocks - num_blocks_for_inodes));
        num_data_blocks = num_blocks - num_blocks_for_inodes;
        int arr_free_blocks[num_data_blocks];
        arr_free_blocks[0] = -1;
        arr_free_blocks[1] = -1;
        // Curr byte pointer to see if went past block size - init to inode because 0th node not free (fs_header)
        int byte_pointer = sizeof(struct fs_header);
        int block_i = 2;
        int i;
        // TODO: Check indirect free blocks work
        void *curr_block = malloc(BLOCKSIZE);
        memcpy(curr_block, first_block, BLOCKSIZE);

        // First initialize all data blocks as 0
        for (i = num_blocks_for_inodes+1; i < num_data_blocks; i++) {
            arr_free_blocks[i] = 0;
        }
        for (i = 1; i < num_inodes+1; i++) {
            // Mark as not free because used for inodes
            arr_free_blocks[block_i] = -1;
            TracePrintf(0, "Block we're looking at: %d\n", block_i);
            // If byte pointer past block size, get the next block
            if (byte_pointer > BLOCKSIZE) {
                if ((c = ReadSector(block_i, curr_block)) == ERROR) {
                    return ERROR;
                }
                block_i++;
                byte_pointer = 0;
            }
            // Get the current inode
            struct inode *curr_inode = (struct inode *) (curr_block + byte_pointer);
            byte_pointer += sizeof(struct inode);
            // Mark if inode free or not
            if (curr_inode->type == INODE_FREE) {
                TracePrintf(0, "%dth inode is free\n", i);
                arr_free_inodes[i] = 0;
            } else {
                arr_free_inodes[i] = 1;
            }

            // Mark the free blocks as free or not - first go through direct
            int j;
            int *direct_from_inode = (int *) curr_inode->direct;
            for (j = 0; j < NUM_DIRECT; j++) {
                if (direct_from_inode[j] != 0) {
                    arr_free_blocks[direct_from_inode[j]] = 1;
                } else {
                    arr_free_blocks[direct_from_inode[j]] = 0;
                } 
            }
            // Then go through the block of indirect
            int indirect_block_num = curr_inode->indirect;
            if (indirect_block_num != 0) {
                arr_free_blocks[indirect_block_num] = 1;
                void *temp_block_for_indirect = malloc(BLOCKSIZE);
                if ((c = ReadSector(indirect_block_num, temp_block_for_indirect)) == ERROR) {
                    return ERROR;
                }
                
                // Iterate through all the blocks in indirect
                for (j = 0; j < BLOCKSIZE / (int) sizeof(int); j++) {
                    int curr_block_num = *((int *) (temp_block_for_indirect + j * sizeof(int)));
                    TracePrintf(0, "Indirect blocked used up: %d\n");
                    if (curr_block_num != 0)
                        arr_free_blocks[curr_block_num] = 1;
                }
                free(temp_block_for_indirect);
            }
        }

        // We are done building the free lists
        free(curr_block);
        // copy to pointers I hate this stupid thing
        free_inodes = malloc(sizeof(arr_free_inodes));
        memcpy(free_inodes, &arr_free_inodes, sizeof(arr_free_inodes));
        free_blocks = malloc(sizeof(arr_free_blocks));
        memcpy(free_blocks, &arr_free_blocks, sizeof(arr_free_blocks));

        // Set current directory as root
        current_inode_directory = ROOTINODE;

        // Receive from the client a message
        while (1) {
            struct my_msg *message = malloc(sizeof(struct my_msg));
            int client_pid;
            while ((client_pid = Receive((void *) message)) == 0);
            TracePrintf(0, "Client pid: %d\n", client_pid);

            int message_type = message->type;

            char pathname[MAXPATHNAMELEN];
            char first_char;
            char *token;
            switch (message_type) {
                case OPEN_M:
                    TracePrintf(0, "In open inside YFS\n");
                    CopyFrom(client_pid, (void *) &pathname, message->ptr, (int) message->data1);
                    
                    // go down  root node
                    // check the path name one by one.
                    // then... open each file, create each file, open their blocks, check each
                    // blockk for the directory entries.
                    // check each block in NUM_DIRECT. TODO!! Test rest of blocks

                    // Split the pathname by /
                    first_char = pathname[0];
                    token = strtok(pathname, "/");

                    // Check if absolute or relative
                    if (first_char == '/') { // absolute
                        // Use root inode
                        int inum_result = check_folder(ROOTINODE, token, ROOTINODE, 1);
                        TracePrintf(0, "opened file at this inum: %d\n", inum_result);
                    } else { // relative
                        int inum_result = check_folder(current_inode_directory, token, current_inode_directory, 1);
                        TracePrintf(0, "opened file at this inum: %d\n", inum_result);
                    }

                    break;
                case CLOSE_M:
                case CREATE_M:
                    // Try creating now
                    TracePrintf(0, "In CREATE inside YFS\n");
                    // Copy the pathname
                    CopyFrom(client_pid, (void *) &pathname, message->ptr, (int) message->data1);
                    // Now have pathname, try to copy into the right folder
                    first_char = pathname[0];
                    token = strtok(pathname, "/");

                    // Absolute path
                    if (first_char == '/') {
                        int inum_result = check_folder(ROOTINODE, token, ROOTINODE, 2);
                        TracePrintf(0, "Created file with this inum: %d\n", inum_result);
                    } else { // relative
                        int inum_result = check_folder(current_inode_directory, token, current_inode_directory, 2);
                        TracePrintf(0, "opened file at this inum: %d\n", inum_result);
                    }

                    // // Begin at root node
                    // if (pathname[0] == '/' || current_inode_directory == ROOTINODE) {
                    //     // Assume is just file and doesn't already exist
                    //     struct dir_entry entry_to_ins = {.inum = find_free_inode((int *) free_inodes)};
                    //     // copy pathname to the entry
                    //     char *token = strtok(pathname, "/");
                    //     strncpy(entry_to_ins.name, token, strlen(token));
                    //     // add null terminators to end
                    //     if (sizeof(pathname) < DIRNAMELEN) {
                    //         memset(entry_to_ins.name + sizeof(pathname), '\0', DIRNAMELEN - sizeof(pathname));
                    //     }
                    //     // Edit root inode page and root inode
                    //     TracePrintf(0, "Block id: %d\n", (int) root_inode->direct[0]);
                    //     void *block_to_edit = malloc(BLOCKSIZE);
                    //     // TODO: See if all the direct blocks are full, check indirect
                    //     if ((c = ReadSector((int) root_inode->direct[0], block_to_edit)) == ERROR) {
                    //         return ERROR;
                    //     }
                    //     // Check if any dir_entries are 0
                    //     int num_dir_entries = root_inode->size / sizeof(struct dir_entry);
                    //     int j;
                    //     for (j = 0; j < num_dir_entries; j++) {
                    //         struct dir_entry *curr_dir_entry = (struct dir_entry *) (block_to_edit + j * sizeof(struct dir_entry));
                    //         if (curr_dir_entry->inum == 0) { // This means this dir is free
                    //             TracePrintf(0, "Found an empty dir entry, inserting\n");
                    //             curr_dir_entry->inum = entry_to_ins.inum;
                    //             memcpy(curr_dir_entry->name, entry_to_ins.name, DIRNAMELEN);
                    //             break;
                    //         }
                    //     }
                    //     // If didn't find anything, append
                    //     if (j == num_dir_entries) {
                    //         memcpy(block_to_edit + num_dir_entries * sizeof(struct dir_entry), &entry_to_ins, sizeof(entry_to_ins));
                    //         root_inode->size += sizeof(struct dir_entry);
                    //     }
                    //     // Write to the sector
                    //     if ((c = WriteSector((int) root_inode->direct[0], block_to_edit)) == ERROR) {
                    //         return ERROR;
                    //     }
                    //     // Write to the root inode
                    //     // Write to the sector
                    //     if ((c = WriteSector(ROOTINODE, first_block)) == ERROR) {
                    //         return ERROR;
                    //     }

                    // }

                    
                    break;
                case READ_M:
                case WRITE_M:
                case SEEK_M:
                case LINK_M:
                case UNLINK_M:
                case READLINK_M:
                case MKDIR_M:
                    // TODO: Add '.' if have / at the end
                    // Try creating now
                    TracePrintf(0, "In MKDIR inside YFS\n");
                    // Copy the pathname
                    CopyFrom(client_pid, (void *) &pathname, message->ptr, (int) message->data1);
                    // Now have pathname, try to copy into the right folder
                    first_char = pathname[0];
                    token = strtok(pathname, "/");

                    // Absolute path
                    if (first_char == '/') {
                        int inum_result = check_folder(ROOTINODE, token, ROOTINODE, 4);
                        TracePrintf(0, "Created directory with this inum: %d\n", inum_result);
                    } else { // relative
                        int inum_result = check_folder(current_inode_directory, token, current_inode_directory, 4);
                        TracePrintf(0, "opened directory at this inum: %d\n", inum_result);
                    }
                    break;
                case RMDIR_M:
                    TracePrintf(0, "In RMDIR inside YFS\n");
                    // Copy the pathname
                    CopyFrom(client_pid, (void *) &pathname, message->ptr, (int) message->data1);
                    // Now have pathname, try to remove into the right folder
                    first_char = pathname[0];
                    token = strtok(pathname, "/");

                    // Absolute path
                    if (first_char == '/') {
                        int inum_result = check_folder(ROOTINODE, token, ROOTINODE, 5);
                        TracePrintf(0, "Removed directory with this inum: %d\n", inum_result);
                    } else { // relative
                        int inum_result = check_folder(current_inode_directory, token, current_inode_directory, 5);
                        TracePrintf(0, "Removed directory at this inum: %d\n", inum_result);
                    }
                    break;
                case CHDIR_M:
                case STAT_M:
                case SYNC_M:
                case SHUTDOWN_M:
                    // See page - write cached to disk
                    // TODO: Informative message
                    TracePrintf(0, "EXITING\n");
                    struct my_msg exit_message = {.type = SHUTDOWN_M, .data1 = -1};
                    Reply((void *) &exit_message, client_pid);
                    Exit(1);
                    break;
                default:
                    TracePrintf(0, "CRITICIAL: Invalid message sent!\n");
            }

            // Just reply with some message
            struct my_msg some_message;
            Reply((void *) &some_message, client_pid);

            // Free the current message
            free(message);
        }

        // Free block holder
        free(first_block);
    }

    free(free_blocks);
    free(free_inodes);
    return 0;
}



// Function to find the next free inode
int find_free_inode() {
    int i;
    TracePrintf(0, "Looking for a free inode size %d\n", (int) sizeof(free_inodes));
    // Start at 2 because root node is 1, 0 is fs_header
    for (i = 2; i < num_inodes+1; i++) {
        TracePrintf(0, "we are at index %d for free inodes\n", i);
        if (free_inodes[i] == 0) {
            free_inodes[i] = 1;
            TracePrintf(0, "Inode that we are gonna use: %d\n", i);
            return i;
        }
    }
    // no free inodes left, return error
    return ERROR;
}

// Function to find the next free block
int find_free_block() {
    int i;
    TracePrintf(0, "Looking for a free block\n");
    // Start at 2 because OS block 0, 1 is for inodes and fs_header
    for (i = 2; i < num_data_blocks; i++) {
        if (free_blocks[i] == 0) {
            free_blocks[i] = 1;
            TracePrintf(0, "Block that we are gonna use: %d\n", i);
            return i;
        }
    }
    // no free inodes left, return error
    return ERROR;
}

// return inode number to open
// have different modes: 1 - open a FILE, 2 - create a FILE, 3 - open a DIR, 4 - create a DIR, 5 - remove a dir
int check_folder(int curr_inum, char *curr_pathname, int parent_inum, int mode) {
    struct inode *curr_inode = (struct inode *) (first_block + curr_inum * sizeof(struct inode));
    // Check if this is the last one
    char *temp_pathname = malloc(strlen(curr_pathname) + 1);
    strcpy(temp_pathname, curr_pathname);
    curr_pathname = strtok(NULL, "/");
    int reached_file = 0;
    // Check that is directory type
    TracePrintf(0, "Current inode we're at: %d\n", curr_inum);
    if (curr_inode->type != INODE_DIRECTORY) {
        // Not a directory
        return ERROR;
    }

    // Check if reached base case
    if (curr_pathname == NULL) {
        // Reached the end, temp_pathname should be a file - now need to look for it in this folder
        reached_file = 1;
    }

    int num_dir_entries = curr_inode->size / (int) sizeof(struct dir_entry);
    TracePrintf(0, "Num dir entries: %d\n", num_dir_entries);
    int curr_dir_index = 0;

    // Go through direct entries first
    int i;
    for (i = 0; i < NUM_DIRECT; i++) {
        TracePrintf(0, "Looking in direct\n");
        if (curr_inode->direct[i] == 0) {
            // Not a valid block -> we didn't find it. Return ERROR
            free(temp_pathname);
            return ERROR;
        } else {
            // Check this current direct block for our folder
            void *current_block = malloc(BLOCKSIZE);
            TracePrintf(0, "Current direct block we're at: %d\n", curr_inode->direct[i]);
            if ((c = ReadSector((int) curr_inode->direct[i], current_block)) == ERROR) {
                free(current_block);
                free(temp_pathname);
                return ERROR;
            }

            int j;
            TracePrintf(0, "iterating through block with dir entries\n");
            for (j = 0; j < num_dir_entries; j++) {
                // If went past the num_dir_entries in total, didn't find it
                if (curr_dir_index >= num_dir_entries) {
                    free(current_block);
                    free(temp_pathname);
                    return ERROR;
                }
                curr_dir_index++;
                struct dir_entry *curr_dir_entry = (struct dir_entry *) (current_block
                + j * sizeof(struct dir_entry));
                TracePrintf(0, "This dir entry: %s\n", curr_dir_entry->name);
                // Compare entry with the current folder we're in
                // Use strncmp because curr_dir_entry isn't null terminated
                if (strncmp(curr_dir_entry->name, temp_pathname, strlen(temp_pathname)) == 0) {
                    // Match, recursively call if not file
                    if (reached_file == 0) {
                        TracePrintf(0, "Found folder, recursively calling\n");
                        free(current_block);
                        free(temp_pathname);
                        return check_folder(curr_dir_entry->inum, curr_pathname, curr_inum, mode);
                    } else {
                        TracePrintf(0, "FOUND FILE - file num is %d\n", curr_dir_entry->inum);
                        // Make sure is file
                        struct inode *file_inode = (struct inode *) (first_block + curr_dir_entry->inum * sizeof(struct inode));
                        TracePrintf(0, "Block number: %d\n", file_inode->direct[0]);
                        if (mode == 1) { // we are opening the file
                            if (file_inode->type == INODE_REGULAR) {
                                free(current_block);
                                free(temp_pathname);
                                return curr_dir_entry->inum;
                            } else {
                                free(current_block);
                                free(temp_pathname);
                                return ERROR;
                            }
                        } else if (mode == 5) { // delete directory
                            if (file_inode->type == INODE_DIRECTORY) {
                                // don't remove . and .. and root
                                if (curr_dir_entry->inum == ROOTINODE || strcmp(curr_dir_entry->name, ".") == 0 || strcmp(curr_dir_entry->name, "..") == 0 ){
                                    TracePrintf(0, "Trying to remove root or . or ..\n");
                                    return ERROR;
                                }
                                remove_inode(curr_dir_entry->inum);
                                curr_dir_entry->inum = 0;
                                memset(curr_dir_entry->name, '\0', DIRNAMELEN);
                                // Write this to block
                                if ((c = WriteSector((int) curr_inode->direct[i], current_block)) == ERROR) {
                                    return ERROR;
                                }

                                return 0;
                            }
                        } else if (mode == 2 || mode == 4) { // Found file/directory but already created, return error
                            TracePrintf(0, "ERROR: file already created!\n");
                            free(current_block);
                            free(temp_pathname);
                            return ERROR;
                        }
                        
                        
                    }
                }

                // if is last file and we are creating file, check the dir_entry to see if empty
                // If is create, edit
                if (reached_file && (mode == 2 || mode == 4)) {
                    // Only if we're still in the block we can append a dir_entry
                    if (curr_dir_entry->inum == 0) {
                        struct dir_entry dir_entry_to_insert;
                        if (mode == 2) { // create a file
                            dir_entry_to_insert = create_file_dir(temp_pathname, 1, parent_inum, 0);
                        } else { // create a dir
                            dir_entry_to_insert = create_file_dir(temp_pathname, 0, parent_inum, 0);
                        }
                        // Change current dir entry to this one
                        memcpy(curr_dir_entry, &dir_entry_to_insert, sizeof(struct dir_entry));
                        // Write to disk
                        if ((c = WriteSector(curr_inode->direct[i], current_block)) == ERROR) {
                            return ERROR;
                        }

                        return dir_entry_to_insert.inum;
                    } 
                }
                
            }

            // If is create, append
            if (reached_file && (mode == 2 || mode == 4)) {
                // Only if we're still in the block we can append a dir_entry
                if (j * sizeof(struct dir_entry) < BLOCKSIZE) {
                    TracePrintf(0, "location: %d\n", j);
                    int old_j = j;
                    struct dir_entry dir_entry_to_insert;
                    if (mode == 2) { // create a file
                        dir_entry_to_insert = create_file_dir(temp_pathname, 1, parent_inum, 1);
                    } else { // create a dir
                        dir_entry_to_insert = create_file_dir(temp_pathname, 0, parent_inum, 1);
                    }
                    TracePrintf(0, "Dir entry: %d %s\n", dir_entry_to_insert.inum, dir_entry_to_insert.name);
                    // Change current dir entry to this one
                    TracePrintf(0, "%p\n", current_block + old_j * sizeof(struct dir_entry));
                    memcpy(current_block + old_j * sizeof(struct dir_entry), &dir_entry_to_insert, sizeof(struct dir_entry));
                    // Write to disk
                    TracePrintf(0, "Inum to write to: %d\n", curr_inode->direct[i]);
                    if ((c = WriteSector(curr_inode->direct[i], current_block)) == ERROR) {
                      return ERROR;
                    }

                    return dir_entry_to_insert.inum;
                }
            }
            
            free(current_block);
            
        }
    }

    // Didn't find it, now iterate through indirect block
    void *indirect_block = malloc(BLOCKSIZE);
    if ((c = ReadSector((int) curr_inode->indirect, indirect_block)) == ERROR) {
        free(indirect_block);
        free(temp_pathname);
        return ERROR;
    }

    // Holder for each block we go through inside the indirect block
    void *indirect_block_block = malloc(BLOCKSIZE);
    for (i = 0; i < BLOCKSIZE / (int) sizeof(int); i++) {
        int *indirect_inum = (int *) (indirect_block + i * sizeof(int));
        if (*indirect_inum == 0) {
            free(indirect_block);
            free(temp_pathname);
            free(indirect_block_block);
            return ERROR;
        }
        if ((c = ReadSector(*indirect_inum, indirect_block_block)) == ERROR) {
            free(indirect_block);
            free(temp_pathname);
            free(indirect_block_block);
            return ERROR;
        }
        int j;
        for (j = 0; j < num_dir_entries; j++) {
            // If went past the num_dir_entries in total, didn't find it
            if (curr_dir_index >= num_dir_entries) {
                free(indirect_block);
                free(temp_pathname);
                free(indirect_block_block);
                return ERROR;
            }
            curr_dir_index++;
            struct dir_entry *curr_dir_entry = (struct dir_entry *) (indirect_block_block
            + j * sizeof(struct dir_entry));
            // Compare entry with the current folder we're in
            // Use strncmp because curr_dir_entry isn't null terminated
            if (strncmp(curr_dir_entry->name, temp_pathname, strlen(temp_pathname)) == 0) {
                // Match, recursively call if not file
                if (reached_file == 0) {
                    free(indirect_block);
                    free(temp_pathname);
                    free(indirect_block_block);
                    TracePrintf(0, "Found folder, recursively calling\n");
                    return check_folder(curr_dir_entry->inum, curr_pathname, curr_inum, mode);
                } else {
                    TracePrintf(0, "FOUND FILE - file num is %d\n", curr_dir_entry->inum);
                    // Make sure is file
                    struct inode *file_inode = (struct inode *) (first_block + curr_dir_entry->inum * sizeof(struct inode));
                    if (mode == 1) {
                        if (file_inode->type == INODE_REGULAR) {
                            free(indirect_block);
                            free(temp_pathname);
                            free(indirect_block_block);
                            return curr_dir_entry->inum;
                        } else {
                            free(indirect_block);
                            free(temp_pathname);
                            free(indirect_block_block);
                            return ERROR;
                        }
                    } else if (mode == 5) { // delete directory
                        if (file_inode->type == INODE_DIRECTORY) {
                            // don't remove . and .. and root
                            if (curr_dir_entry->inum == ROOTINODE || strcmp(curr_dir_entry->name, ".") == 0 || strcmp(curr_dir_entry->name, "..") == 0 ){
                                TracePrintf(0, "Trying to remove root or . or ..\n");
                                free(indirect_block);
                                free(temp_pathname);
                                free(indirect_block_block);
                                return ERROR;
                            }
                            remove_inode(curr_dir_entry->inum);
                            curr_dir_entry->inum = 0;
                            memset(curr_dir_entry->name, '\0', DIRNAMELEN);
                            // Write this to block
                            if ((c = WriteSector(*indirect_inum, indirect_block_block)) == ERROR) {
                                free(indirect_block);
                                free(temp_pathname);
                                free(indirect_block_block);
                                return ERROR;
                            }

                            return 0;
                        }
                    } else if (mode == 2 || mode == 4) { // Found file/directory but already created, return error
                        TracePrintf(0, "ERROR: file already created!\n");
                        free(indirect_block);
                        free(temp_pathname);
                        free(indirect_block_block);
                        return ERROR;
                    }
                    
                } 
            }

            // if is last file and we are creating file, check the dir_entry to see if empty
            // If is create, edit
            if (reached_file && (mode == 2 || mode == 4)) {
                // Only if we're still in the block we can append a dir_entry
                if (curr_dir_entry->inum == 0) {
                    struct dir_entry dir_entry_to_insert;
                    if (mode == 2) { // create a file
                        dir_entry_to_insert = create_file_dir(temp_pathname, 1, parent_inum, 0);
                    } else { // create a dir
                        dir_entry_to_insert = create_file_dir(temp_pathname, 0, parent_inum, 0);
                    }
                    // Change current dir entry to this one
                    memcpy(curr_dir_entry, &dir_entry_to_insert, sizeof(struct dir_entry));
                    // Write to disk
                    if ((c = WriteSector(*indirect_inum, indirect_block_block)) == ERROR) {
                        return ERROR;
                    }

                    return dir_entry_to_insert.inum;
                } 
            }

        }

        // If is create, append
        if (reached_file && (mode == 2 || mode == 4)) {
            // Only if we're still in the block we can append a dir_entry
            if (j * sizeof(struct dir_entry) < BLOCKSIZE) {
                TracePrintf(0, "location: %d\n", j);
                struct dir_entry dir_entry_to_insert;
                if (mode == 2) { // create a file
                    dir_entry_to_insert = create_file_dir(temp_pathname, 1, parent_inum, 1);
                } else { // create a dir
                    dir_entry_to_insert = create_file_dir(temp_pathname, 0, parent_inum, 1);
                }
                // Change current dir entry to this one
                memcpy(indirect_block_block + j * sizeof(struct dir_entry), &dir_entry_to_insert, sizeof(struct dir_entry));
                // Write to disk
                TracePrintf(0, "Inum to write to: %d\n", *indirect_inum);
                if ((c = WriteSector(*indirect_inum, indirect_block_block)) == ERROR) {
                    return ERROR;
                }

                return dir_entry_to_insert.inum;
            }
        }

        
       

        
    }

    free(indirect_block_block);
    free(indirect_block);
    free(temp_pathname);

    return ERROR;
}

// Create file or directory, return dir_entry (to append or overwrite)
// file_dir: 1 if file, 0 if dir
// append boolean
struct dir_entry create_file_dir(char *actual_filename, int file_dir, int parent_inum, int append) {
    struct dir_entry entry_to_ins = {.inum = find_free_inode()};
    TracePrintf(0, "creating folder or file %s\n", actual_filename);
    strncpy(entry_to_ins.name, actual_filename, strlen(actual_filename));
    // add null terminators to end
    if (sizeof(actual_filename) < DIRNAMELEN) {
        memset(&entry_to_ins.name + strlen(actual_filename) + 1, '\0', DIRNAMELEN - strlen(actual_filename));
    }
    // Add size to parent
    if (append == 1) {
        TracePrintf(0, "appending to parent\n");
        struct inode *parent_inode = (struct inode *) (first_block + parent_inum * sizeof(struct inode));
        parent_inode->size += sizeof(struct dir_entry);
        // Will overwrite first block in end
    }
    // Configure inode
    struct inode *insert_inode = (struct inode *) (first_block + entry_to_ins.inum * sizeof(struct inode));
    // Find next free block
    insert_inode->direct[0] = find_free_block();
    TracePrintf(0, "New block: %d\n", insert_inode->direct[0]);
    // Check if file or directory
    if (file_dir == 1) {
        insert_inode->type = INODE_REGULAR;
    } else { // if directory, add . and ..
        insert_inode->type = INODE_DIRECTORY;
        void *temp_block_for_insert = malloc(BLOCKSIZE);
        if ((c = ReadSector(insert_inode->direct[0], temp_block_for_insert)) == ERROR) {
            free(temp_block_for_insert);
            entry_to_ins.inum = -1;
            return entry_to_ins;
        }
        // .
        struct dir_entry this_entry = {.inum = entry_to_ins.inum, .name = "."};
        // ..
        struct dir_entry parent_entry = {.inum = parent_inum, .name = ".."};
        memcpy(temp_block_for_insert, &this_entry, sizeof(struct dir_entry));
        memcpy(temp_block_for_insert + sizeof(struct dir_entry), &parent_entry, sizeof(struct dir_entry));
        // increase size
        insert_inode->size += 2 * sizeof(struct dir_entry);
        if ((c = WriteSector(insert_inode->direct[0], temp_block_for_insert)) == ERROR) {
            free(temp_block_for_insert);
            entry_to_ins.inum = -1;
            return entry_to_ins;
        }
        free(temp_block_for_insert);
    }

    // Edit inode, overwrite first block
    if ((c = WriteSector(1, first_block)) == ERROR) {
        entry_to_ins.inum = -1;
        return entry_to_ins;
    }

    TracePrintf(0, "creating/opening file\n");

    return entry_to_ins;
}

// return ERROR if error, otherwise return 1
int remove_inode(int inum) {
    struct inode *inode_to_remove = (struct inode *) (first_block + inum * sizeof(struct inode));
    // If folder, ensure there is only . and ..
    if (inode_to_remove->type == INODE_DIRECTORY) {
        // throw error if have more than 2 * dir_entry
        if (inode_to_remove->size > 2 * (int) sizeof(struct dir_entry)) {
            return ERROR;
        }
    }

    // Set inode to free among other things
    free_inodes[inum] = 0;
    inode_to_remove->type = INODE_FREE;
    inode_to_remove->nlink = 0;
    inode_to_remove->reuse = 0;
    inode_to_remove->size = 0;
    
    // Direct Blocks
    int i;
    for (i = 0; i < NUM_DIRECT; i++) {
        free_blocks[inode_to_remove->direct[i]] = 0;
    }
    // Indirect block
    //int indirect_block_num = inode_to_remove->indirect;
    void *indirect_block = malloc(BLOCKSIZE);
    if ((c = ReadSector(inode_to_remove->indirect, indirect_block)) == ERROR) {
        free(indirect_block);
        return ERROR;
    }
    
    for (i = 0; i < BLOCKSIZE / (int) sizeof(int); i++) {
        TracePrintf(0, "Going THROUGH Indirect Block Entries!\n");
        int *currentBlock = (int *) (indirect_block + i * sizeof(int));
        if (*currentBlock != 0) {
            free_blocks[*currentBlock] = 0;
        }
    }
    
    free(indirect_block);
    // Write to first block
    if ((c = WriteSector(1, first_block)) == ERROR) {
        return ERROR;
    }

    return 1;
}