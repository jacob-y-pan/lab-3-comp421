#include <comp421/filesystem.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <stdlib.h>

#include "yfs_header.h"

int num_inodes;
int num_blocks;

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
        ReadSector(1, (void *) first_block);
        // Get Fs_header
        struct fs_header *main_fs_header = (struct fs_header *) first_block;
        TracePrintf(0, "Fs_header values: num inodes: %d\n", main_fs_header->num_inodes);
        // Make array of free inodes - all init'd to 0
        num_inodes = main_fs_header->num_inodes;
        num_blocks = main_fs_header->num_blocks;
        
        // Make num_inodes + 1 because fs_header
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
        int free_blocks[num_blocks - num_blocks_for_inodes];
        // Curr byte pointer to see if went past block size - init to 1 * inode because first i node not free
        int byte_pointer = sizeof(struct inode);
        int block_i = 2;
        int i;
        void *curr_block = first_block;
        for (i = 1; i < num_inodes+1; i++) {
            // If byte pointer past block size, get the next block
            if (byte_pointer > BLOCKSIZE) {
                ReadSector(block_i, curr_block);
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
                free_inodes[i] = i;
            }

            // Mark the free blocks as free or not - first go through direct
            int j;
            int *direct_from_inode = (int *) curr_inode->direct;
            for (j = 0; j < NUM_DIRECT; j++) {
                if (direct_from_inode[j] != 0) {
                    free_blocks[direct_from_inode[j]] = direct_from_inode[j];
                } else {
                    free_blocks[direct_from_inode[j]] = 0;
                }
            }
            // Then go through the block of indirect
            
        }

        (void) free_blocks;
        (void) free_inodes;
        (void) start_data_blocks;

        // Free block holder
        free(curr_block);

        struct my_msg *message = malloc(sizeof(struct my_msg));
        TracePrintf(0, "parent\n");
        Receive((void *) message);
        TracePrintf(0, "%d\n", message->data1);
    }

    return 0;
}


// // Function to find the next free inode
// int find_free_inode() {
//     int i;
//     for (i = 0; i < )
// }
