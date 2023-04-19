#include <comp421/filesystem.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <comp421/iolib.h>

#include "yfs_header.h"

int num_inodes;
int num_blocks;
int num_data_blocks;
// Global arrays because f stupid functions
int *free_inodes;
int *free_blocks;

// Store the first block since will always stay constant
void *first_block;

// Holder for checking readSector and WriteSector errors
int c;

// Functions
int find_free_inode();
int find_free_block();
int check_folder(int curr_inum, char *curr_pathname, int parent_inum, int mode, int link_inum);
struct dir_entry create_file_dir(char *actual_filename, int file_dir, int parent_inum, int append, int link_inum);
int open_file_inode(struct dir_entry *this_dir_entry);
int remove_inode(struct inode *parent_inode, struct dir_entry *this_dir_entry, int this_index, void *this_block, int direct_indirect);
int unlink_inode(struct inode *parent_inode, struct dir_entry *this_dir_entry, int this_index, void *this_block, int direct_indirect);

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
        TracePrintf(0, "Making sure msg size is 32: %d\n", (int) sizeof(struct my_msg));
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
        num_data_blocks = num_blocks - num_blocks_for_inodes; //possible error
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
            int inum_result;
            int reply_result;
            // Message to reply back with
            struct my_msg reply_message;

            int inode_check;
            int number_to_read;
            int current_position;
            void * buf_readTo;
            void * buf_writeTo;
            void * first_block; 
            

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
                        inum_result = check_folder(ROOTINODE, token, ROOTINODE, 1, 0);
                        TracePrintf(0, "opened file at this inum: %d\n", inum_result);
                    } else { // relative
                        inum_result = check_folder(message->data2, token, message->data2, 1, 0);
                        TracePrintf(0, "opened file at this inum: %d\n", inum_result);
                    }

                    reply_result = inum_result;

                    break;
                case CLOSE_M:
                    // Do nothing because should not get close_m
                    break;
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
                        inum_result = check_folder(ROOTINODE, token, ROOTINODE, 2, 0);
                        TracePrintf(0, "Created file with this inum: %d\n", inum_result);
                    } else { // relative
                        inum_result = check_folder(message->data2, token, message->data2, 2, 0);
                        TracePrintf(0, "Created file at this inum: %d\n", inum_result);
                    }

                    first_block = malloc(BLOCKSIZE);
                    if ((c = ReadSector(1, (void *) first_block)) == ERROR) {
                        return ERROR;
                    }

                    reply_result = inum_result;
                    break;
                case READ_M:
                    //Reads a specific file at the desired 
                    TracePrintf(0, "Inside the Read function\n");

                    // Reads a specific inode at the right time;

                    // READ message: Specific data fields --> data1 (inode number), data2 (read number), data3 (position of the file),
                    // ptr (buffer to read)
                    inode_check = (int) message->data1;
                    number_to_read = (int) message->data2;
                    current_position = (int) message->data3;
                    buf_readTo = (void *) message->ptr;

                    int num_bytes_read;

                    first_block = malloc(BLOCKSIZE);
                    if ((c = ReadSector(1, first_block)) == ERROR) {
                            //free(current_block);bl
                            return ERROR;
                    }
                    
                    struct inode *curr_inode = (struct inode *) (first_block + inode_check * sizeof(struct inode));
                    
                    void *current_block = malloc(BLOCKSIZE);

                    //direct blocks
                    int blockToLookIn = (int) current_position / BLOCKSIZE;
                    TracePrintf(0, " block it's in: %d", blockToLookIn);
                    if(blockToLookIn < NUM_DIRECT)
                    {
                        TracePrintf(0, "Position to Read In Block is in Direct Blocks\n");
                    }
                    else
                    {
                        TracePrintf(0, "Position to Read is in Indirect Blocks\n");
                    }

                    TracePrintf(0, "Current direct : %d\n", (int) current_position / BLOCKSIZE);
                    // TracePrintf(0, "Current direct : %d\n", );
                    int positionInBlock = current_position % BLOCKSIZE;
                    //testing if greater than size of all bytes/size of file!
                    if ((current_position % BLOCKSIZE) + number_to_read < curr_inode->size)
                    {
                        // can read!
                        TracePrintf(0, "READ 1: Enough to Read");
                    }
                    else {
                        number_to_read = curr_inode->size - (current_position % BLOCKSIZE);
                    }

                    // READ Sector of the corresponding region.
                    
                    //CASE 1
                    if( (blockToLookIn < NUM_DIRECT) && ((positionInBlock + number_to_read) < BLOCKSIZE) )
                    {
                        TracePrintf(0, "Read Normally in First Case\n");
                        //assert(number)
                        //Read the Direct i normally
                        if ((c = ReadSector((int) curr_inode->direct[blockToLookIn], current_block)) == ERROR) {
                            free(current_block);
                            return ERROR;
                        }

                        
                        //number of bytes to read in the block --> current block pointer
                        // block To Read. memcpy into the buffer for receiving.
                        //CopyTo(current_block + positionInBlock, blockToRead, number_to_read);
                        CopyTo(client_pid, buf_readTo, current_block + positionInBlock, number_to_read);
                        num_bytes_read = number_to_read;
                    }
                    else if( (blockToLookIn + 1 < NUM_DIRECT) && ((positionInBlock + number_to_read) > BLOCKSIZE)){
                        void * readBuffer = malloc(number_to_read+1);

                        int readFirstBlock = BLOCKSIZE - positionInBlock;
                         //Read the Direct i normally
                        if ((c = ReadSector((int) curr_inode->direct[blockToLookIn], current_block)) == ERROR) {
                            free(current_block); 
                            return ERROR;
                        }
    
                        memcpy(readBuffer, current_block + positionInBlock, readFirstBlock); //read till the end

                        if ((c = ReadSector((int) curr_inode->direct[blockToLookIn], current_block)) == ERROR) {
                            free(current_block);
                            return ERROR;
                        }

                        int readSecondBlock = number_to_read - (BLOCKSIZE-positionInBlock);
                        
                         if ((c = ReadSector((int) curr_inode->direct[blockToLookIn + 1], current_block)) == ERROR) {
                            free(current_block);
                            return ERROR;
                        }
                        
                        memcpy(readBuffer + (BLOCKSIZE-positionInBlock), current_block, readSecondBlock);
                        

                        // Read the Indirect Block now

                        CopyTo(client_pid, buf_readTo, readBuffer,number_to_read);


                    }
                    else{       

                        //indirect blocks
                          // read the Indirect Block for Block Directory
                        if ((c = ReadSector((int) curr_inode->indirect, current_block)) == ERROR) {
                            //free(current_block);
                            return ERROR;
                        }
                        // actual number of blocks?
                        int blockIndirect = blockToLookIn - NUM_DIRECT;
      
                        int *indirect_inum = (int *) (current_block + blockIndirect * sizeof(int));
                        (void) indirect_inum;
                        //TODO?
                        //current Block is actually pointing to the block of indirect Bloock!
                        if ((c = ReadSector(blockIndirect, current_block)) == ERROR) {
                            free(current_block);
                            return ERROR;
                        }


                        
                        indirect_inum = (int *) (current_block + blockIndirect * sizeof(int));
                        (void) indirect_inum;
                        //TODO?
                        //current Block is actually pointing to the block of indirect Bloock!
                        if ((c = ReadSector(blockIndirect, current_block)) == ERROR) {
                            free(current_block);
                            return ERROR;
                        }

                    } 

                    TracePrintf(0, "End of READ File\n");
                    
                    free(current_block);

                    reply_result = num_bytes_read;
                    break;
                case WRITE_M:
                    //Writes at a specific file at the desired location.
                     TracePrintf(0, "Inside the Write function\n");

                    // Reads a specific inode at the right time;

                    // READ message: Specific data fields --> data1 (inode number), data2 (read number), data3 (position of the file),
                    // ptr (buffer to read)
                    inode_check = (int) message->data1;
                    int number_to_write = (int) message->data2;
                    current_position = (int) message->data3;
                    buf_writeTo = (void *) message->ptr;

                    TracePrintf(0, "Writing file with inode num %d\n", inode_check);
                    TracePrintf(0, "Amount to Write %d\n", number_to_write);
                    // read bugffer + size, to check

    
                    first_block = malloc(BLOCKSIZE);
                    if ((c = ReadSector(1, first_block)) == ERROR) {
                            //free(current_block);bl
                            return ERROR;
                    }

                    curr_inode = (struct inode *) (first_block + inode_check * sizeof(struct inode));
                    TracePrintf(0, " current_position: %d\n", current_position);
                    
                    current_block = malloc(BLOCKSIZE);

                    TracePrintf(0, "Current direct : %d\n", (int) current_position / BLOCKSIZE);
                    // block to look in
                    blockToLookIn = (int) current_position / BLOCKSIZE;
                    TracePrintf(0, " block it's in: %d\n", blockToLookIn);
                    if(blockToLookIn < NUM_DIRECT)
                    {
                        TracePrintf(0, "Position to Read In Block is in Direct Blocks\n");
                    }
                    else
                    {
                        TracePrintf(0, "Position to Read is in Indirect Blocks\n");
                    }

                    positionInBlock = current_position % BLOCKSIZE; 
                    //direct blocks

                    //you need new block
                    TracePrintf(0, "Size of file: %d\n", curr_inode->size);

                    // Pointer to what we want to write
                    char *buffer_holder = malloc(number_to_write);
                    CopyFrom(client_pid, buffer_holder, buf_writeTo, number_to_write);

                    TracePrintf(0, "Buffer: %s\n", buffer_holder);

                    int bytes_to_write_counter = number_to_write;
                    if ( (current_position + number_to_write > curr_inode->size) ) 
                    {
                        // Need more blocks      
                        TracePrintf(0, "What we are writing is greater than size, need to increase size\n");
                        if (curr_inode->size % BLOCKSIZE != 0) {  // we can use this last block
                            void *block_to_write = malloc(BLOCKSIZE);
                            if ((c = ReadSector(curr_inode->direct[blockToLookIn], block_to_write)) == ERROR) {
                                free(block_to_write);
                                return ERROR;
                            }

                            if ((c = ReadSector(curr_inode->direct[blockToLookIn], block_to_write)) == ERROR) {
                                free(block_to_write);
                                return ERROR;
                            }
                            free(block_to_write);
                            blockToLookIn += 1;
                        }     
                        int num_blocks_needed = (int) number_to_write / BLOCKSIZE + 1;
                        int i;
                        TracePrintf(0, "Num blocks need: %d\n", num_blocks_needed);
                        // go through direct blocks
                        for (i = blockToLookIn; i < blockToLookIn+num_blocks_needed; i++) {
                            if (i >= NUM_DIRECT) {
                                // Need to get indirect block
                                TracePrintf(0, "Needing to get indirect block\n");
                                break;
                            } else {
                                TracePrintf(0, "Getting block for this size\n");
                                int new_block_num = find_free_block();
                                curr_inode->direct[i] = new_block_num;

                                // Write as much as we can
                                TracePrintf(0, "Writing now\n");
                                // Use calloc because init'ing to 0
                                void *block_to_write = calloc(BLOCKSIZE, 1);
                                int min_to_write = BLOCKSIZE < number_to_write ? BLOCKSIZE : number_to_write;
                                memcpy(block_to_write, buffer_holder, min_to_write);
                                if ((c = WriteSector(curr_inode->direct[i], block_to_write)) == ERROR) {
                                    free(block_to_write);
                                    return ERROR;
                                }

                                bytes_to_write_counter -= min_to_write;

                                free(block_to_write);
                            }
                        }

                        // Check if wrote everything: if did, return
                        if (bytes_to_write_counter == 0) {
                            TracePrintf(0, "Finished\n");
                            return bytes_to_write_counter;
                        }
                            // // number of blocks needed:

                            // int endPosition = (int) (current_position + number_to_write  - curr_inode->size);
                            // //                               - (# bytes in last block)) / BLOCKSIZE;
                            // // number of direct blocks needed?
                            // int INDIRECT_NEEDED;
                            // if(current_position + number_to_write <= BLOCKSIZE * NUM_DIRECT){
                            //     //  might need more direct blocks.
                                
                            //     int number_direct  = number_to_write + (current_position);
                            //     (void) number_direct;     

                            // } else {
                            //     //indirect blocks needed
                            //     INDIRECT_NEEDED = (int) (current_position 
                            //                     + number_to_write - BLOCKSIZE * NUM_DIRECT) / BLOCKSIZE;
                            //     (void) INDIRECT_NEEDED;
                            // }
                            // int DIRECT_BLOCKS_USED = 0 ;
                            

                            // // get the number of direct blocks
                            // int INDIRECT_BLOCKS = 0;

                            // // get number of direct blocks 
                            // int current_direct_block = (int) curr_inode->size / NUM_DIRECT;
                           

                            // int indirectBlockForNode; 
                            
                            // // if you need nmore direct blocks (by checking which direct block you're on)
                            // int i;
                            // for(i = 0; i < INDIRECT_NEEDED; i++) {

                            //     //get one indirect block
                            //     indirectBlockForNode = find_free_block();
                            //     curr_inode->indirect = find_free_block();
                            //     if ((c = WriteSector(1, first_block)) == ERROR) {
                            //         //  free(temp_pathname);
                            //         return ERROR;
                            //     }

                                
                            //     // get two indirect block
                            //     int indirectBlock2ForNode;
                            //     (void) indirectBlock2ForNode;

                            //     // Indiret Block --> 
                            //     // Get Indirect Blocks

                            //     // Get Free Indirect Blocks

                            //     // write Into Indirect Block

                            //     // ReadSector(indirectBlock2ForNode, ...)
                            //     void * indirect_block;
                            //     void *indirect_block_block = malloc(BLOCKSIZE);
                            //     (void) indirect_block_block;
                            //     for (i = 0; i < BLOCKSIZE / (int) sizeof(int); i++) {
                            //         int *indirect_inum = (int *) (indirect_block + i * sizeof(int));
                            //         (void) indirect_inum;
                            //     }
                                
                            //     // write in another indirect block



                            //     // write in indirect block

                            //     // get memory.



                            // }


                        // Complicated case
                        //int num_blocks_needed  = (int) ( / BLOCKSIZE);
                        // extra blocka

                        // depending on number of blocks you need, get one, or two...
                        
                        // grab in the free_block_list; sort through; grab the first five extra blocks;
                        // if(num_blocks_needed > NUM_DIRECT)
                        // {
                        //     int num_indirect_blocks = num_blocks_needed - NUM_DIRECT;

            
                        // }
                    }
                    else{
                        // just write in block
                         if ((c = ReadSector((int) curr_inode->direct[blockToLookIn], current_block)) == ERROR) {
                            //free(current_block);
                            return ERROR;
                        }

                        // EDIT Current Block
                        //number of bytes to read in the block --> current block pointer
                        // block To Read. memcpy into the buffer for receiving.
                        //CopyTo(current_block + positionInBlock, blockToRead, number_to_read);
                        TracePrintf(0, "Writing now\n");
                        CopyFrom(client_pid, current_block + positionInBlock, buf_writeTo, number_to_write);

                        if ((c = WriteSector((int) curr_inode->direct[blockToLookIn], current_block)) == ERROR) {
                            free(current_block);
                            return ERROR;
                        }

                        
                    }
/*************************** This is Old Code */
                    /**                   */
                     //direct blocks
                    
                    blockToLookIn = 0;
                    //need additional block... write definition says that you reached end of the file 
                    // if (positi)


                    //testing if greater than size of all bytes/size of file!
                    if( (current_position % BLOCKSIZE) + number_to_write < curr_inode->size)
                    {
                        // can read!
                        TracePrintf(0, "READ 1: Enough to Read");
                        
                    }
                    else{
                        number_to_write = curr_inode->size - (current_position % BLOCKSIZE);

                    }

                    //int case = 0;
                    //(void * )case;
                    
                    //

                    // WRITE Sector of the corresponding region.
                    
                    //CASE 2
                    if( (blockToLookIn < NUM_DIRECT) && ((positionInBlock + number_to_write) < BLOCKSIZE) )
                    {
                        TracePrintf(0, "Read Normally in First Case");
                        //assert(number)
                        //Read the Direct i normally
                       

                    }
                    //case 2
                    else if( (blockToLookIn + 1 < NUM_DIRECT) && ((positionInBlock + number_to_write) > BLOCKSIZE)){
                        void * writeBuffer = malloc(number_to_write+1);

                        CopyFrom(client_pid, writeBuffer, buf_writeTo, number_to_write);

                        int writeFirstBlock = BLOCKSIZE - positionInBlock;
                         //Read the Direct i normally
                        if ((c = ReadSector((int) curr_inode->direct[blockToLookIn], current_block)) == ERROR) {
                            free(current_block);
                            return ERROR;
                        }
    
                        memcpy(current_block + positionInBlock, writeBuffer, writeFirstBlock); //read till the end
                        //TODO: WriteSector?
                        if ((c = ReadSector((int) curr_inode->direct[blockToLookIn], current_block)) == ERROR) {
                            free(current_block);
                            return ERROR;
                        }

                        int writeSecondBlock = number_to_write - (BLOCKSIZE-positionInBlock);
                        
                         if ((c = ReadSector((int) curr_inode->direct[blockToLookIn + 1], current_block)) == ERROR) {
                            free(current_block);
                            return ERROR;
                        }
                        
                        memcpy(current_block, writeBuffer + (BLOCKSIZE-positionInBlock), writeSecondBlock);
                        
                        //TODO WriteSector?

                         if ((c = WriteSector((int) curr_inode->direct[blockToLookIn + 1], current_block)) == ERROR) {
                            free(current_block);
                            return ERROR;
                        }
                        // Read the Indirect Block now

                        

                    }
                    //case 3
                    else{
                        
                        // read the Indirect Block for Block Directory
                        if ((c = ReadSector((int) curr_inode->indirect, current_block)) == ERROR) {
                            //free(current_block);
                            return ERROR;
                        }

                        // indirect block num
                        int blockIndirect = blockToLookIn - NUM_DIRECT;

                        
                        int *actualIndirectBlock = (int *) (current_block + blockIndirect * sizeof(int));
                        

                        //current Block is actually pointing to the block of indirect Bloock!
                        if ((c = ReadSector(*actualIndirectBlock, current_block)) == ERROR) {
                            free(current_block);
                            return ERROR;
                        }

                        //position in Block correlates to the actual byte number
                        CopyTo(client_pid, current_block + positionInBlock, buf_writeTo, number_to_write);
                       
                       // Current Block is Written back into The Sector.
                        if ((c = WriteSector(*actualIndirectBlock, current_block)) == ERROR) {
                            free(current_block);
                            return ERROR;
                        }
                    }

                    free(current_block);
                    break;

                    TracePrintf(0, "End of READ File");
                case SEEK_M:
                    TracePrintf(0, "in Seek inside YFS");
                    // Return size
                    struct inode *this_inode = (struct inode *) (first_block + message->data2 * sizeof(struct inode));
                    inum_result = this_inode->size;
                    break;
                case LINK_M:
                    TracePrintf(0, "in LINK inside YFS");
                    // Pathname = oldname, pathname2 = newname
                    CopyFrom(client_pid, (void *) &pathname, message->ptr, (int) message->data1);
                    char pathname2[MAXPATHNAMELEN];
                    CopyFrom(client_pid, (void *) &pathname2, message->ptr2, (int) message->data3);
                    // Find inode of oldname
                    first_char = pathname[0];
                    token = strtok(pathname, "/");

                    // Absolute path
                    if (first_char == '/') {
                        inum_result = check_folder(ROOTINODE, token, ROOTINODE, 1, 0);
                        TracePrintf(1, "Opened old file with this inum: %d\n", inum_result);
                    } else { // relative
                        inum_result = check_folder(message->data2, token, message->data2, 1, 0);
                        TracePrintf(1, "Opened old file at this inum: %d\n", inum_result);
                    }

                    // Find inode of newname
                    first_char = pathname2[0];
                    token = strtok(pathname2, "/");
                    // We'll say we're "creating" a file, but with link_inum != 0 we link
                    // Absolute path
                    if (first_char == '/') {
                        inum_result = check_folder(ROOTINODE, token, ROOTINODE, 2, inum_result);
                        TracePrintf(1, "Opened old file with this inum: %d\n", inum_result);
                    } else { // relative
                        inum_result = check_folder(message->data2, token, message->data2, 2, inum_result);
                        TracePrintf(1, "Opened old file at this inum: %d\n", inum_result);
                    }

                    if (inum_result >= 0) {
                        reply_result = 0;
                    } else {
                        reply_result = ERROR;
                    }
                    
                    break;
                case UNLINK_M:
                    TracePrintf(0, "In UNLINK inside YFS\n");
                    // Copy the pathname
                    CopyFrom(client_pid, (void *) &pathname, message->ptr, (int) message->data1);
                    // Now have pathname, try to remove into the right folder
                    first_char = pathname[0];
                    token = strtok(pathname, "/");

                    // Use mode 7 to unlink
                    // Absolute path
                    if (first_char == '/') {
                        inum_result = check_folder(ROOTINODE, token, ROOTINODE, 7, 0);
                        TracePrintf(0, "Unlinked file with status: %d\n", inum_result);
                    } else { // relative
                        inum_result = check_folder(message->data2, token, message->data2, 7, 0);
                        TracePrintf(0, "Unlinked file with status: %d\n", inum_result);
                    }

                    if (inum_result >= 0) {
                        reply_result = 0;
                    } else {
                        reply_result = ERROR;
                    }

                    break;
                case READLINK_M:
                case MKDIR_M:
                    // TODO: Add '.' if have / at the end
                    // Try creating now
                    TracePrintf(0, "In MKDIR inside YFS\n");
                    // Copy the pathname
                    CopyFrom(client_pid, (void *) &pathname, message->ptr, (int) message->data1);
                    TracePrintf(0, "Path creating: %s\n", pathname);
                    // Now have pathname, try to copy into the right folder
                    first_char = pathname[0];
                    token = strtok(pathname, "/");

                    // Absolute path
                    if (first_char == '/') {
                        inum_result = check_folder(ROOTINODE, token, ROOTINODE, 4, 0);
                        TracePrintf(0, "Created directory with this inum: %d\n", inum_result);
                    } else { // relative
                        inum_result = check_folder(message->data2, token, message->data2, 4, 0);
                        TracePrintf(0, "opened directory at this inum: %d\n", inum_result);
                    }

                    if (inum_result >= 0) {
                        reply_result = 0;
                    } else {
                        reply_result = ERROR;
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
                        inum_result = check_folder(ROOTINODE, token, ROOTINODE, 5, 0);
                        TracePrintf(0, "Removed directory with status: %d\n", inum_result);
                    } else { // relative
                        inum_result = check_folder(message->data2, token, message->data2, 5, 0);
                        TracePrintf(0, "Removed directory with status: %d\n", inum_result);
                    }

                    if (inum_result >= 0) {
                        reply_result = 0;
                    } else {
                        reply_result = ERROR;
                    }
                    break;
                case CHDIR_M:
                    TracePrintf(0, "In CHDIR inside YFS\n");
                    // Copy the pathname
                    CopyFrom(client_pid, (void *) &pathname, message->ptr, (int) message->data1);
                    // Now have pathname, try to remove into the right folder
                    first_char = pathname[0];
                    token = strtok(pathname, "/");
                    // Absolute path
                    if (first_char == '/') {
                        inum_result = check_folder(ROOTINODE, token, ROOTINODE, 6, 0);
                        TracePrintf(0, "Changed directory with status: %d\n", inum_result);
                    } else { // relative
                        inum_result = check_folder(message->data2, token, message->data2, 6, 0);
                        TracePrintf(0, "Changed directory with status: %d\n", inum_result);
                    }
                    break;
                case STAT_M:
                    TracePrintf(0, "In STAT inside YFS\n");
                    // Copy the pathname
                    CopyFrom(client_pid, (void *) &pathname, message->ptr, (int) message->data1);
                    struct Stat statbufholder;
                    // Now have pathname, try to remove into the right folder
                    first_char = pathname[0];
                    token = strtok(pathname, "/");
                    // Can just use open because returning inum
                    // Absolute path
                    if (first_char == '/') {
                        inum_result = check_folder(ROOTINODE, token, ROOTINODE, 1, 0);
                    } else { // relative
                        inum_result = check_folder(message->data2, token, message->data2, 1, 0);
                    }
                    // Store into stat
                    struct inode *stat_inode = (struct inode *) (first_block + inum_result * sizeof(struct inode));
                    statbufholder.inum = inum_result;
                    statbufholder.type = stat_inode->type;
                    statbufholder.size = stat_inode->size;
                    statbufholder.nlink = stat_inode->nlink;

                    // Write to the buffer
                    TracePrintf(0, "Stat inode: %d\n", statbufholder.inum);
                    struct Stat *client_buffer = (struct Stat *) message->ptr2;
                    CopyTo(client_pid, (void *) client_buffer, &statbufholder, sizeof(statbufholder));

                    if (inum_result >= 0) {
                        reply_result = 0;
                    } else {
                        reply_result = ERROR;
                    }
                    break;
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

            // Clean up data
            reply_message.data1 = message_type;
            reply_message.data2 = reply_result;
            reply_message.data3 = inum_result;
            memset(&pathname, '\0', MAXPATHNAMELEN);
            Reply((void *) &reply_message, client_pid);

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
// have different modes: 1 - open a FILE, 2 - create a FILE, 3 - open a DIR (not valid), 4 - create a DIR, 
// 5 - remove a dir, 6 - change to dir
// link_inum is 0 - don't do anything if != 0 means we're linking
int check_folder(int curr_inum, char *curr_pathname, int parent_inum, int mode, int link_inum) {
    (void) parent_inum;
    struct inode *curr_inode = (struct inode *) (first_block + curr_inum * sizeof(struct inode));
    // Check if this is the last one
    char *temp_pathname = malloc(strlen(curr_pathname) + 1);
    strcpy(temp_pathname, curr_pathname);
    curr_pathname = strtok(NULL, "/");
    int reached_file = 0;
    // Check that is directory type
    TracePrintf(0, "Current inode we're at: %d\n", curr_inum);
    TracePrintf(0, "Current folder we're in: %s\n", temp_pathname);
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
            // if create or mkdir, can allocate more space
            if (mode == 2 || mode == 4) {
                curr_inode->direct[i] = find_free_block();
                // Write to inodes
                if ((c = WriteSector(1, first_block)) == ERROR) {
                    free(temp_pathname);
                    return ERROR;
                }

            } else {
                // Not a valid block -> we didn't find it. Return ERROR
                TracePrintf(0, "Invalid block! Returning.\n");
                free(temp_pathname);
                return ERROR;
            }
        }
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
        for (j = 0; j < BLOCKSIZE / (int) sizeof(struct dir_entry); j++) {
            // If went past the num_dir_entries in total, didn't find it
            if (curr_dir_index >= num_dir_entries) {
                // if create, break the loop and append
                if (mode == 2 || mode == 4) {
                    break;
                }
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
                    return check_folder(curr_dir_entry->inum, curr_pathname, curr_inum, mode, link_inum);
                } else {
                    TracePrintf(0, "FOUND FILE/FOLDER - file/folder num is %d\n", curr_dir_entry->inum);
                    if (mode == 1) { // we are opening the file
                        TracePrintf(0, "OPening the file\n");
                        free(current_block);
                        free(temp_pathname);
                        return open_file_inode(curr_dir_entry);
                    } else if (mode == 2 || mode == 4) { // Found file/directory but already created, return error
                        TracePrintf(0, "File/directory already created! If is file, clear out\n");
                        if (mode == 2) { // Is file, can work with it
                            TracePrintf(0, "Truncating size to 0\n");
                            struct inode *file_inode = (struct inode *) (first_block + curr_dir_entry->inum * sizeof(struct inode));
                            if (file_inode->type == INODE_REGULAR) {
                                file_inode->size = 0;
                                free(current_block);
                                free(temp_pathname);
                                return curr_dir_entry->inum;
                            }
                        }
                        free(current_block);
                        free(temp_pathname);
                        return ERROR;
                    } else if (mode == 5) { // delete directory
                        struct inode *file_inode = (struct inode *) (first_block + curr_dir_entry->inum * sizeof(struct inode));
                        if (file_inode->type == INODE_DIRECTORY) {
                            TracePrintf(0, "Deleting the directory\n");
                            int temp = remove_inode(curr_inode, curr_dir_entry, i, current_block, 1);
                            free(current_block);
                            free(temp_pathname);
                            return temp;
                        }
                    } else if (mode == 6) {
                        TracePrintf(0, "Switching to this inode\n");
                        free(current_block);
                        free(temp_pathname);
                        return curr_dir_entry->inum;
                    } else if (mode == 7) {
                        TracePrintf(0, "Unlinking this file\n");
                        free(current_block);
                        free(temp_pathname);
                        return unlink_inode(curr_inode, curr_dir_entry, i, current_block, 1);
                    } else {
                        TracePrintf(0, "ERROR: No action for this dir_entry!\n");
                        free(current_block);
                        free(temp_pathname);
                        return ERROR;
                    }
                    
                }
            }

            // if is last file in recursive function and we are creating file, check the dir_entry to see if empty
            // If is create, edit
            if (reached_file && (mode == 2 || mode == 4)) {
                // Only if we're still in the block we can append a dir_entry
                if (curr_dir_entry->inum == 0) {
                    TracePrintf(0, "Something is empty, override it\n");
                    struct dir_entry dir_entry_to_insert;
                    if (mode == 2) { // create a file
                        dir_entry_to_insert = create_file_dir(temp_pathname, 1, curr_inum, 0, link_inum);
                    } else { // create a dir
                        dir_entry_to_insert = create_file_dir(temp_pathname, 0, curr_inum, 0, link_inum);
                    }
                    TracePrintf(0, "does it print here first\n");
                    // Change current dir entry to this one
                    memcpy(curr_dir_entry, &dir_entry_to_insert, sizeof(struct dir_entry));
                    // Write to disk
                    if ((c = WriteSector((int) curr_inode->direct[i], current_block)) == ERROR) {
                        free(current_block);
                        free(temp_pathname);
                        return ERROR;
                    }
                    TracePrintf(0, "does it print here third\n");
                    free(current_block);
                    free(temp_pathname);
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
                    dir_entry_to_insert = create_file_dir(temp_pathname, 1, curr_inum, 1, link_inum);
                } else { // create a dir
                    dir_entry_to_insert = create_file_dir(temp_pathname, 0, curr_inum, 1, link_inum);
                }
                TracePrintf(0, "Dir entry: %d %s\n", dir_entry_to_insert.inum, dir_entry_to_insert.name);
                // Change current dir entry to this one
                TracePrintf(0, "%p\n", current_block + old_j * sizeof(struct dir_entry));
                memcpy(current_block + old_j * sizeof(struct dir_entry), &dir_entry_to_insert, sizeof(struct dir_entry));
                // Write to disk
                TracePrintf(0, "Copied\n");
                TracePrintf(0, "Inum to write to: %d\n", curr_inode->direct[i]);
                if ((c = WriteSector(curr_inode->direct[i], current_block)) == ERROR) {
                    free(current_block);
                    free(temp_pathname);
                    return ERROR;
                }

                free(current_block);
                free(temp_pathname);

                return dir_entry_to_insert.inum;
            }
        }
        
        free(current_block);
            
    }

    // Didn't find it, now iterate through indirect block
    // If indirect block is 0 and we want to create, allocate new block
    if (curr_inode->indirect == 0) {
        if (mode == 2 || mode == 4) {
            TracePrintf(0, "Allocating for indirect block\n");
            curr_inode->indirect = find_free_block();
            if ((c = WriteSector(1, first_block)) == ERROR) {
                free(temp_pathname);
                return ERROR;
            }
        } else {
            TracePrintf(0, "ERROR: Indirect block 0, cannot open\n");
            return ERROR;
        }
    }
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
            if (mode == 2 || mode == 4) {
                TracePrintf(0, "Allocating for block inside indirect\n");
                void *temp_p = indirect_block + i * sizeof(int);
                int free_b = find_free_block();
                memcpy(temp_p, &free_b, sizeof(int));
                if ((c = WriteSector((int) curr_inode->indirect, indirect_block)) == ERROR) {
                    free(temp_pathname);
                    return ERROR;
                }
            } else {
                TracePrintf(0, "ERROR: Indirect block 0, cannot open\n");
                return ERROR;
            }
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
        for (j = 0; j < (int) sizeof(struct dir_entry); j++) {
            // If went past the num_dir_entries in total, didn't find it
            if (curr_dir_index >= num_dir_entries) {
                // if create, break the loop and append
                if (mode == 2 || mode == 4) {
                    break;
                }
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
                    return check_folder(curr_dir_entry->inum, curr_pathname, curr_inum, mode, link_inum);
                } else {
                    TracePrintf(0, "FOUND FILE - file num is %d\n", curr_dir_entry->inum);
                    // Make sure is file
                    if (mode == 1) {
                        free(indirect_block);
                        free(temp_pathname);
                        free(indirect_block_block);
                        return open_file_inode(curr_dir_entry);
                    } else if (mode == 2 || mode == 4) { // Found file/directory but already created, return error
                        TracePrintf(0, "File/directory already created! If is file, clear out\n");
                        if (mode == 2) { // Is file, can work with it
                            TracePrintf(0, "Truncating size to 0\n");
                            struct inode *file_inode = (struct inode *) (first_block + curr_dir_entry->inum * sizeof(struct inode));
                            if (file_inode->type == INODE_REGULAR) {
                                file_inode->size = 0;
                                free(indirect_block);
                                free(temp_pathname);
                                free(indirect_block_block);
                                return curr_dir_entry->inum;
                            }
                        }
                        free(indirect_block);
                        free(temp_pathname);
                        free(indirect_block_block);
                        return ERROR;
                    } else if (mode == 5) { // delete directory
                        struct inode *file_inode = (struct inode *) (first_block + curr_dir_entry->inum * sizeof(struct inode));
                        if (file_inode->type == INODE_DIRECTORY) {
                            int temp = remove_inode(curr_inode, curr_dir_entry, *indirect_inum, indirect_block_block, 0);
                            free(indirect_block);
                            free(temp_pathname);
                            free(indirect_block_block);
                            return temp;
                        }
                    } else if (mode == 6) {
                        TracePrintf(0, "Switching to this inode\n");
                        free(indirect_block);
                        free(temp_pathname);
                        free(indirect_block_block);
                        return curr_dir_entry->inum;
                    } else if (mode == 7) {
                        TracePrintf(0, "Unlinking this file\n");
                        free(indirect_block);
                        free(temp_pathname);
                        free(indirect_block_block);
                        return unlink_inode(curr_inode, curr_dir_entry, *indirect_inum, indirect_block_block, 0);
                    } else {
                        TracePrintf(0, "ERROR: No action for this dir_entry!\n");
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
                        dir_entry_to_insert = create_file_dir(temp_pathname, 1, curr_inum, 0, link_inum);
                    } else { // create a dir
                        dir_entry_to_insert = create_file_dir(temp_pathname, 0, curr_inum, 0, link_inum);
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
                    dir_entry_to_insert = create_file_dir(temp_pathname, 1, curr_inum, 1, link_inum);
                } else { // create a dir
                    dir_entry_to_insert = create_file_dir(temp_pathname, 0, curr_inum, 1, link_inum);
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

// Open an inode
int open_file_inode(struct dir_entry *this_dir_entry) {
    struct inode *file_inode = (struct inode *) (first_block + this_dir_entry->inum * sizeof(struct inode));
    // Make sure is file
    if (file_inode->type == INODE_REGULAR) {
        return this_dir_entry->inum;
    } else {
        return ERROR;
    }
}

// Create file or directory, return dir_entry (to append or overwrite)
// file_dir: 1 if file, 0 if dir
// append boolean
struct dir_entry create_file_dir(char *actual_filename, int file_dir, int parent_inum, int append, int link_inum) {
    TracePrintf(0, "creating folder or file %s\n", actual_filename);
    struct dir_entry null_entry = {.inum = -1, .name = ""};
    // throw error if filename > DIRNAMELEN
    if (strlen(actual_filename) > DIRNAMELEN) {
        TracePrintf(0, "ERROR: Filename too long\n");
        return null_entry;
    }
    struct dir_entry entry_to_ins;
    if (link_inum != 0 && file_dir == 1) {
        entry_to_ins = (struct dir_entry) {.inum = link_inum};
    } else {
        entry_to_ins = (struct dir_entry) {.inum = find_free_inode()};
    }
    // make everything null terminated for now
    memset(&entry_to_ins.name, '\0', DIRNAMELEN);
    strncpy(entry_to_ins.name, actual_filename, strlen(actual_filename));
    
    TracePrintf(0, "Name: %s\n", entry_to_ins.name);
    // Add size to parent
    if (append == 1) {
        TracePrintf(0, "appending to parent\n");
        struct inode *parent_inode = (struct inode *) (first_block + parent_inum * sizeof(struct inode));
        parent_inode->size += sizeof(struct dir_entry);
        TracePrintf(0, "parent size: %d\n", parent_inode->size / sizeof(struct dir_entry));
        // Will overwrite first block in end
    }
    // Configure inode
    struct inode *insert_inode = (struct inode *) (first_block + entry_to_ins.inum * sizeof(struct inode));
    // Check if link
    if (link_inum != 0 && file_dir == 1) {
        insert_inode->nlink += 1;
    } else {
        // Find next free block
        insert_inode->direct[0] = find_free_block();
        TracePrintf(0, "New block: %d\n", insert_inode->direct[0]);
    }
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
        insert_inode->nlink += 1;
        // ..
        struct dir_entry parent_entry = {.inum = parent_inum, .name = ".."};
        struct inode *parent_inode = (struct inode *) (first_block + parent_inum * sizeof(struct inode));
        parent_inode->nlink += 1;

        TracePrintf(0, "Folder created: . inum: %d, .. inum: %d\n", this_entry.inum, parent_entry.inum);

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

// return ERROR if error, otherwise return 0
int remove_inode(struct inode *parent_inode, struct dir_entry *this_dir_entry, int this_index, void *this_block, int direct_indirect) {
    // don't remove . and .. and root
    if (this_dir_entry->inum == ROOTINODE || strcmp(this_dir_entry->name, ".") == 0 || strcmp(this_dir_entry->name, "..") == 0 ) {
        TracePrintf(0, "Trying to remove root or . or ..\n");
        return ERROR;
    }
    int inum = this_dir_entry->inum;
    struct inode *inode_to_remove = (struct inode *) (first_block + inum * sizeof(struct inode));
    // If folder, ensure there is only . and ..
    if (inode_to_remove->type == INODE_DIRECTORY) {
        // throw error if have more than 2 * dir_entry
        if (inode_to_remove->size > 2 * (int) sizeof(struct dir_entry)) {
            return ERROR;
        }
    }

    // reduce size of parent_inode because removing dir_entry
    parent_inode->size -= (int) sizeof(struct dir_entry);

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
        // TracePrintf(0, "Going THROUGH Indirect Block Entries!\n");
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

    // Set everything to empty in this dir_entry
    TracePrintf(0, "File name: %s\n", this_dir_entry->name);
    this_dir_entry->inum = 0;
    memset(this_dir_entry->name, '\0', DIRNAMELEN);
    
    // Write this to block
    int block_num;
    if (direct_indirect == 1) {
        block_num = parent_inode->direct[this_index];
    } else {
        block_num = this_index;
    }
    TracePrintf(0, "Block removed: %d, %d %p\n", this_index, block_num, this_block);
    if ((c = WriteSector(block_num, this_block)) == ERROR) {
        return ERROR;
    }

    TracePrintf(0, "Finished removing\n");

    return 0;
}

int unlink_inode(struct inode *parent_inode, struct dir_entry *this_dir_entry, int this_index, void *this_block, int direct_indirect) {
    struct inode *this_inode = (struct inode *) (first_block + this_dir_entry->inum * sizeof(struct inode));

    if (this_inode->type == INODE_REGULAR) { // Check is file and not directory
        
        this_inode->nlink -= 1;
        if (this_inode->nlink == 0) { // Delete this file
            // TODO: Just free inode?
            free_inodes[this_dir_entry->inum] = 0;
        }
        
        // remove directory entry
        this_dir_entry->inum = 0;
        memset(this_dir_entry->name, '\0', DIRNAMELEN);

        // Removed directory entry, write to block
        int block_num;
        if (direct_indirect == 1) {
            block_num = parent_inode->direct[this_index];
        } else {
            block_num = this_index;
        }
        if ((c = WriteSector(block_num, this_block)) == ERROR) {
            return ERROR;
        }


        // Changed file_node, write to block
        if ((c = WriteSector(1, first_block)) == ERROR) {
            return ERROR;
        }

        return 0;
    } else {
        TracePrintf(0, "ERROR: Needs to be file type, not directory!\n");
        return ERROR;
    }
}