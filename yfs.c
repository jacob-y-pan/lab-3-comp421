#include <comp421/filesystem.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yfs_header.h"

int num_inodes;
int num_blocks;

// Store the current folder we're in (with inode)?
int current_inode_directory;

// Functions
int find_free_inode(int *free_inodes_arr);

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
        void *first_block = malloc(BLOCKSIZE);
        int c;
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
        int free_inodes[num_inodes+1];
        // Mark 0th as not free (fs_header)
        free_inodes[0] = -1;
        // Mark 1st as not free (root inode)
        free_inodes[ROOTINODE] = ROOTINODE;
        // Scan all the other inodes to see if free
        struct inode *root_inode = (struct inode *) (first_block + sizeof(struct fs_header));
        // Get ready to get all the blocks needed for all the inodes - can infer from the first block of root
        int num_blocks_for_inodes = root_inode->direct[0] - 1;
        // In case we need it: start of the data blocks
        int start_data_blocks = root_inode->direct[0];
        // Init free blocks - since concerned about only data blocks, just subtract those
        // 0 = free 1 = not free -1 = never can be free
        int free_blocks[num_blocks - num_blocks_for_inodes];
        // Curr byte pointer to see if went past block size - init to inode because 0th node not free (fs_header)
        int byte_pointer = sizeof(struct fs_header);
        int block_i = 2;
        int i;
        // TODO: Check indirect free blocks work
        void *curr_block = malloc(BLOCKSIZE);
        memcpy(curr_block, first_block, BLOCKSIZE);
        for (i = 1; i < num_inodes+1; i++) {
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
                free_inodes[i] = 0;
            } else {
                free_inodes[i] = 1;
            }

            // Mark the free blocks as free or not - first go through direct
            int j;
            int *direct_from_inode = (int *) curr_inode->direct;
            for (j = 0; j < NUM_DIRECT; j++) {
                if (direct_from_inode[j] != 0) {
                    free_blocks[direct_from_inode[j]] = 1;
                } else {
                    free_blocks[direct_from_inode[j]] = 0;
                } 
            }
            // Then go through the block of indirect
            int indirect_block_num = curr_inode->indirect;
            if (indirect_block_num != 0) {
                free_blocks[indirect_block_num] = indirect_block_num;
                void *temp_block_for_indirect = malloc(BLOCKSIZE);
                if ((c = ReadSector(indirect_block_num, temp_block_for_indirect)) == ERROR) {
                    return ERROR;
                }
                
                // Iterate through all the blocks in indirect
                for (j = 0; j < BLOCKSIZE / (int) sizeof(int); j++) {
                    int curr_block_num = *((int *) (temp_block_for_indirect + j * sizeof(int)));
                    TracePrintf(0, "Indirect blocked used up: %d\n");
                    if (curr_block_num != 0)
                        free_blocks[curr_block_num] = 1;
                }
                free(temp_block_for_indirect);
            }
        }

        // We are done building the free lists
        free(curr_block);

        // Set current directory as root
        current_inode_directory = ROOTINODE;

        // Receive from the client a message
        struct my_msg *message = malloc(sizeof(struct my_msg));
        int client_pid = Receive((void *) message);
        TracePrintf(0, "Client pid: %d\n", client_pid);

        int message_type = message->type;

        char pathname[MAXPATHNAMELEN];
        switch (message_type) {
            case OPEN_M:
                CopyFrom(client_pid, (void *) &pathname, message->ptr, (int) message->data1);

                //Open correct folder();
            
            case CLOSE_M:
            case CREATE_M:
                // Try creating now
                // Copy the pathname
                CopyFrom(client_pid, (void *) &pathname, message->ptr, (int) message->data1);
                // Now have pathname, try to copy into the right folder
                // TODO: Handle folders, rn just copying into root

                // Begin at root node
                if (pathname[0] == '/' || current_inode_directory == ROOTINODE) {
                    // Assume is just file and doesn't already exist
                    struct dir_entry entry_to_ins = {.inum = find_free_inode((int *) free_inodes)};
                    // copy pathname to the entry
                    strncpy(entry_to_ins.name, pathname, sizeof(pathname));
                    // add null terminators to end
                    if (sizeof(pathname) < DIRNAMELEN) {
                        memset(entry_to_ins.name + sizeof(pathname), '\0', DIRNAMELEN - sizeof(pathname));
                    }
                    // Edit root inode page and root inode
                    TracePrintf(0, "Block id: %d\n", (int) root_inode->direct[0]);
                    void *block_to_edit = malloc(BLOCKSIZE);
                    if ((c = ReadSector((int) root_inode->direct[0], block_to_edit)) == ERROR) {
                        return ERROR;
                    }
                    // Check if any dir_entries are 0
                    int num_dir_entries = root_inode->size / sizeof(struct dir_entry);
                    int j;
                    for (j = 0; j < num_dir_entries; j++) {
                        struct dir_entry *curr_dir_entry = (struct dir_entry *) (block_to_edit + j * sizeof(struct dir_entry));
                        if (curr_dir_entry->inum == 0) { // This means this dir is free
                            TracePrintf(0, "Found an empty dir entry, inserting\n");
                            curr_dir_entry->inum = entry_to_ins.inum;
                            memcpy(curr_dir_entry->name, entry_to_ins.name, DIRNAMELEN);
                            break;
                        }
                    }
                    // If didn't find anything, append
                    if (j == num_dir_entries) {
                        memcpy(block_to_edit + num_dir_entries * sizeof(struct dir_entry), &entry_to_ins, sizeof(entry_to_ins));
                        root_inode->size += sizeof(struct dir_entry);
                    }
                    // Write to the sector
                    if ((c = WriteSector((int) root_inode->direct[0], block_to_edit)) == ERROR) {
                        return ERROR;
                    }
                    // Write to the root inode
                    // Write to the sector
                    if ((c = WriteSector(ROOTINODE, first_block)) == ERROR) {
                        return ERROR;
                    }

                }
                

                
                break;
        }

        (void) free_blocks;
        (void) start_data_blocks;

        // Free the current message
        free(message);

        // Free block holder
        free(first_block);
    }

    return 0;
}


// Function to find the next free inode
int find_free_inode(int *free_inodes_arr) {
    int i;
    TracePrintf(0, "Looking for a free inode\n");
    // Start at 2 because root node is 1, 0 is fs_header
    for (i = 2; i < (int) sizeof(free_inodes_arr); i++) {
        if (free_inodes_arr[i] == 0) {
            free_inodes_arr[i] = 1;
            return i;
        }
    }
    // no free inodes left, return error
    return ERROR;
}
