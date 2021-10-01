#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h> // added for open()
#include <unistd.h> // added for close()

#include "disk.h"
#include "fs.h"

#define RDIR_ENTRY_LEN 32  // in bytes
#define FAT_EOC 0xFFFF
#define SIGNATURE_LEN 8
struct superblock{
	uint8_t  signature[SIGNATURE_LEN];  //Signature equal to "ECS150FS"
	uint16_t totalb;	    //Total amount of blocks of virtual disk.
	uint16_t rdb_index;     //Root directory block index
	uint16_t bs_index;      //Data block start index
	uint16_t datab_amount;  //Amount of data blocks
	uint8_t  FAT_num;       //Number of blocks for FAT
	uint8_t  padding[4079]; //Ununsed padding
}__attribute__((packed)); 

struct root_directory_entry{
	uint8_t	 Filename[FS_FILENAME_LEN]; //Filename(including NULL character)
	uint32_t Filesize;	                //Size of the file(in bytes)
	uint16_t Fdb_index;                 //Index of the first data block
	uint8_t  padding[10];               //Unused/padding
}__attribute__((packed));

struct file_descriptor_entry{
	int offset; 	//The offset of a file descriptor. 
	uint8_t Filename[FS_FILENAME_LEN];
};

/* TODO: Phase 1 */
struct superblock *superblock;
uint16_t *FAT;
struct root_directory_entry *root_directory[FS_FILE_MAX_COUNT];
// array to keep track of opened files
struct file_descriptor_entry *open_file_table[FS_OPEN_MAX_COUNT];

// Returns # of free entries in FAT
int fat_free_count();
// Returns index of first free entry in FAT (first-fit)
int fat_free_index();
// Returns # of free entries in root directory
int rdir_free_count();
// Returns index of first free entry found in rdir
int rdir_free_index();
// Returns rdir index with specified filename
int rdir_filename_index(const char *filename);
// Returns index of first free entry found in open-file table
int OFT_free_index();
// Returns # of free entries in OFT
int OFT_free_count();
// Returns first open-file table index with specified filename
int OFT_filename_index(const char *filename);
// Free memory of rdir up to certain indicated index
void memfree_rdir(int max_index);
// Returns index of a datablock at a file's offset to write to
// Also may extend # of datablocks of file when necessary
int data_block_index_write(int fd, int rdir_index, size_t count);
// Returns index of a datablock at a file's offset to read from
int data_block_index_read(int fd, int rdir_index);
// Returns # of data blocks belonging to a file
int num_blocks(int rdir_index);


int fs_mount(const char *diskname)
{
	/* TODO: Phase 1 */

	// open the virtual disk, using the block API
	if (block_disk_open(diskname) == -1)
		return -1;
	
	// load the meta-information
	// index i to keep track what block index to read info for
	uint8_t i = 0;

	// superblock 
	superblock = malloc(sizeof(struct superblock));
	// check if malloc successful
	if (!superblock)
		return -1;

	if (block_read(i, superblock) == -1) {
		free(superblock);
		return -1;
	}
	// Signature equal to "ECS150FS"
	if (strncmp((char*)(superblock->signature), "ECS150FS\0", SIGNATURE_LEN) != 0) {
		free(superblock);
		return -1;
	}

	// the total amount of block should correspond to what block_disk_count() returns
	if (block_disk_count() != (superblock->totalb)) {
		free(superblock);
		return -1;
	}

	// FAT (size = # of datablocks)
	uint16_t FAT_size = superblock->datab_amount;
	int offset = 0;
	FAT = (uint16_t*)malloc(FAT_size * sizeof(uint16_t));
	// check if malloc successful
	if (!FAT) {
		free(superblock);
		return -1;
	}
	for (i = 1; i < superblock->FAT_num + 1; i++) {
		// make temp buffer to read whole block
		uint16_t buf[BLOCK_SIZE];
		if (block_read(i, buf) == -1)
			return -1;
		// if # fat entries < a block, copy the FAT size # of bytes
		// else copy a blocksize of bytes, decrement FAT size by blocksize, inc offset by
		if (FAT_size < BLOCK_SIZE)
			memcpy(FAT + offset, buf, FAT_size);
		else {
			memcpy(FAT + offset, buf, BLOCK_SIZE);
			FAT_size -= BLOCK_SIZE;
			offset += BLOCK_SIZE;
		}
	}

	// create space for each rdir entry
	for (int j = 0; j < FS_FILE_MAX_COUNT; j++) {
		root_directory[j] = malloc(sizeof(struct root_directory_entry));
		// check if malloc successful
		if (!root_directory[j]) {
			free(superblock);
			free(FAT);
			memfree_rdir(j);
			return -1;
		}
	}
	
	char buf[BLOCK_SIZE];
	offset = 0;
	if (block_read(i, buf) == -1) {
		free(FAT);
		free(superblock);
		return -1;
	}
	for (int j = 0; j < FS_FILE_MAX_COUNT; j++) {
		memcpy(root_directory[j], buf + offset, RDIR_ENTRY_LEN);
		offset += RDIR_ENTRY_LEN;
	}
	return 0;
}

int fs_umount(void)
{
	/* TODO: Phase 1 */

	// Write data before exit
	uint8_t i = 0;
	block_write(i, superblock);
	uint16_t FAT_size = superblock->datab_amount;
	int offset = 0;
	for (i = 1; i < superblock->FAT_num + 1; i++) {
		// make temp buffer to write to whole block
		uint16_t buf[BLOCK_SIZE];
		// if # fat entries < a block, copy the FAT size # of bytes
		// else copy a blocksize of bytes, decrement FAT size by blocksize, inc offset by
		if (FAT_size < BLOCK_SIZE)
			memcpy(buf, FAT + offset, FAT_size);
		else {
			memcpy(buf, FAT + offset, BLOCK_SIZE);
			FAT_size -= BLOCK_SIZE;
			offset += BLOCK_SIZE;
		}
		block_write(i, buf);
	}
	char buf[BLOCK_SIZE];
	offset = 0;
	for (int j = 0; j < FS_FILE_MAX_COUNT; j++) {
		memcpy(buf + offset, root_directory[j], RDIR_ENTRY_LEN);
		offset += RDIR_ENTRY_LEN;
	}
	block_write(i, buf);

    // properly clean all the internal data structures of the FS layer
	memfree_rdir(FS_FILE_MAX_COUNT);
	free(FAT);
	free(superblock);

	// check if file descriptors open still
	if (OFT_free_count() != FS_OPEN_MAX_COUNT)
		return -1;
	// close disk to unmount
	if (block_disk_close() == -1) 
		return -1;

	return 0;
}

int fs_info(void)
{
	/* TODO: Phase 1 */
	
	// Return: -1 if no underlying virtual disk was opened.
	if (block_disk_count() == -1) 
		return -1;

	printf("FS Info:\n");
	printf("total_blk_count=%hu\n", superblock->totalb);
	printf("fat_blk_count=%hhu\n", superblock->FAT_num);
	printf("rdir_blk=%hu\n", superblock->rdb_index);
	printf("data_blk=%hu\n", superblock->bs_index);
	printf("data_blk_count=%hu\n", superblock->datab_amount);
	printf("fat_free_ratio=%d/%hu\n", fat_free_count(), superblock->datab_amount);
	printf("rdir_free_ratio=%d/%d\n", rdir_free_count(), FS_FILE_MAX_COUNT);
	return 0;
}

int fs_create(const char *filename)
{
	/* TODO: Phase 2 */

	// Return: -1 if no FS is currently mounted
	if (block_disk_count() == -1)
		return -1;

	// if @filename is invalid
	if (!filename)
		return -1;

	// its total length cannot exceed %FS_FILENAME_LEN characters 
	if (strlen(filename) > FS_FILENAME_LEN)
		return -1;

	// file named @filename already exists
	if (rdir_filename_index(filename) != -1) 
		return -1;

	// if the root directory already contains %FS_FILE_MAX_COUNT files
	if (rdir_free_count() == 0)
		return -1;

	// find empty entry in rdir
	int free_index = rdir_free_index();
	memcpy(root_directory[free_index]->Filename, filename, FS_FILENAME_LEN);
	root_directory[free_index]->Filesize = 0;
	root_directory[free_index]->Fdb_index = FAT_EOC;
	return 0;
}

int fs_delete(const char *filename)
{
	/* TODO: Phase 2 */

	// Return: -1 if no FS is currently mounted
	if (block_disk_count() == -1)
		return -1;

	// if @filename is invalid
	if (!filename)
		return -1;

	// its total length cannot exceed %FS_FILENAME_LEN characters 
	if (strlen(filename) > FS_FILENAME_LEN)
		return -1;

	// no file named @filename to delete
	int rdir_file_index = rdir_filename_index(filename);
	if (rdir_file_index == -1) 
		return -1;

	// file @filename is currently open
	if (OFT_filename_index(filename) != -1)
		return -1;

	// clear associated metadata for the given file

	// find index of file in root directory
	uint16_t block_index = root_directory[rdir_file_index]->Fdb_index;
	// access entry's first datablock index to 
	// traverse FAT and set FAT entries to 0 (including FAT_EOC)
	if (block_index != FAT_EOC) {
		while(1) {
			if (FAT[block_index] == FAT_EOC) {
				FAT[block_index] = 0;
				break;
			} else {
				uint16_t temp_block_index = block_index;
				block_index = FAT[block_index];
				FAT[temp_block_index] = 0;
			}
		}
	}
	// Remove from rdir (Filename has '\0' as first character)
	memset(root_directory[rdir_file_index], 0, sizeof(struct root_directory_entry));
	root_directory[rdir_file_index]->Filename[0] = '\0';
	return 0;
}

int fs_ls(void)
{
	/* TODO: Phase 2 */

	// Return: -1 if no FS is currently mounted
	if (block_disk_count() == -1)
		return -1;

	printf("FS Ls:\n");
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (root_directory[i]->Filename[0] != '\0') {
			printf("file: %s, size: %u, data_blk: %hu\n", 
			root_directory[i]->Filename, 
			root_directory[i]->Filesize,
			root_directory[i]->Fdb_index);
		}
	}
	return 0;
}

int fs_open(const char *filename)
{
	/* TODO: Phase 3 */

	// Return -1 if no FS is currently mounted
	if (block_disk_count() == -1)
		return -1;

	// Return -1 when filename is invalid
	if (!filename)
		return -1;

	// its total length cannot exceed %FS_FILENAME_LEN characters 
	if (strlen(filename) > FS_FILENAME_LEN)
		return -1;

	// Return -1 when no filename named @filename to open
	if (rdir_filename_index(filename) == -1) 
		return -1;

	// Return -1 if there are already %FS_OPEN_MAX_COUNT files currently open
	int fd = OFT_free_index();
	if (fd == -1)
		return -1;

	// Make memory space for new OFT entry
	open_file_table[fd] = malloc(sizeof(struct file_descriptor_entry));
	// check if malloc successful
	if (!open_file_table[fd])
		return -1;
	// Initialize filename, offset
	memcpy(open_file_table[fd]->Filename, filename, FS_FILENAME_LEN);
	open_file_table[fd]->offset = 0;

	return fd;
}

int fs_close(int fd)
{
	/* TODO: Phase 3 */

	// Return -1 if no FS is currently mounted
	if (block_disk_count() == -1)
		return -1;

	// Return -1 if fd is out of bounds

	if ((fd > FS_OPEN_MAX_COUNT) || (fd < 0))
		return -1;

	// Return -1 if fd is not currently open
	if (!open_file_table[fd])
		return -1;

	// Clear values of the fd entry
	memset(open_file_table[fd], 0, sizeof(struct file_descriptor_entry));
	// free from memory
	free(open_file_table[fd]);
	// set pointer to NULL
	open_file_table[fd] = NULL;
	return 0;
}

int fs_stat(int fd)
{
	/* TODO: Phase 3 */

	// Return -1 if no FS is currently mounted
	if (block_disk_count() == -1)
		return -1;

	// fd out of bounds
	if ((fd > FS_OPEN_MAX_COUNT) || (fd < 0))
		return -1;

	// Return -1 if fd is not currently open
	if (!open_file_table[fd])
		return -1;
	
	// access index of rdir with matching filename of desired fd
	int index = rdir_filename_index((char*)(open_file_table[fd]->Filename));
	// access filesize once rdir index obtained
	uint32_t filesize = root_directory[index]->Filesize;

	return (int)filesize;
}

int fs_lseek(int fd, size_t offset)
{
	/* TODO: Phase 3 */

	// Return -1 if no FS is currently mounted
	if (block_disk_count() == -1)
		return -1;

	// fd out of bounds
	if ((fd > FS_OPEN_MAX_COUNT) || (fd < 0))
		return -1;

	// Return -1 if fd is not currently open
	if (!open_file_table[fd])
		return -1;
	
	// Return -1 if offset is larger than current filesize
	if((int)offset > fs_stat(fd))
		return -1;

	open_file_table[fd]->offset = offset;

	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
	
	// Return -1 if no FS is currently mounted
	if (block_disk_count() == -1)
		return -1;

	// fd out of bounds
	if ((fd > FS_OPEN_MAX_COUNT) || (fd < 0))
		return -1;

	// Return -1 if fd is not currently open
	if (!open_file_table[fd])
		return -1;	
	
	// Return -1 if buff is NULL
	if (!buf)
		return -1;

	// access index of rdir with matching filename of desired fd
	int rdir_index = rdir_filename_index((char*)(open_file_table[fd]->Filename));
	// block index is start point, which block to write to, adjusted by offset
	int block_index = data_block_index_write(fd, rdir_index, count);
	// return 0 bytes written if no space to write
	if (block_index == -1)
		return 0;

	// If starting at a new data block (first write/append new block),
	// update FAT for the file
	if (block_index == fat_free_index()) 
		FAT[block_index] = FAT_EOC;

	int buf_offset = 0;
	int bytes_to_write = count; 
	char bounce_buf[BLOCK_SIZE];
	int write_amount = 0;
	int bb_offset = 0;
	int start_offset = open_file_table[fd]->offset;
	while (1) {
		if ((open_file_table[fd]->offset % BLOCK_SIZE) != 0) {
			// case: writing a block at a offset unaligned with (start of a block)
			if (((open_file_table[fd]->offset + bytes_to_write) % BLOCK_SIZE) != 0) {
				// sub-case: write to offset + bytes_to_write
				write_amount = bytes_to_write;
			} else {
				// sub-case: write rest of block
				write_amount = (BLOCK_SIZE - (open_file_table[fd]->offset % BLOCK_SIZE));
			}
			bb_offset = (open_file_table[fd]->offset % BLOCK_SIZE);
		} else if (bytes_to_write < BLOCK_SIZE) {
			// case: write less than a block (end situations)
			write_amount = bytes_to_write;
			bb_offset = 0;
		} else {
			// case: writing whole blocks
			write_amount = BLOCK_SIZE;
			bb_offset = 0;
		}
		// Read block into bounce buffer
		block_read(block_index, bounce_buf);
		// copy data from buf while keeping necessary read data
		memcpy(bounce_buf + bb_offset, buf + buf_offset, write_amount);
		block_write(block_index, bounce_buf);
		open_file_table[fd]->offset += write_amount;
		bytes_to_write -= write_amount;
		buf_offset += write_amount;

		if (bytes_to_write == 0)
			break;

		// set FAT entries if additional are needed
		if (FAT[block_index] == 0 || FAT[block_index] == FAT_EOC) {
			// case: extending file, reached end, write to new data blocks
			uint16_t temp_index = block_index;
			// Get next free fat entry, break if no more space available
			if (fat_free_index() == -1) {
				FAT[block_index] = FAT_EOC;
				break;
			} else
				block_index = fat_free_index();
			// Update FAT table
			FAT[temp_index] = block_index;
			FAT[block_index] = FAT_EOC;
		} else {
			// case: overwriting existing file space
			// simply move to file's next data block
			block_index = FAT[block_index];
		}
	}
	// Increase filesize input if exceeds end of the file
	if ((start_offset + buf_offset) > (int)(root_directory[rdir_index]->Filesize)) {
		root_directory[rdir_index]->Filesize += 
		((int)(root_directory[rdir_index]->Filesize) - start_offset + buf_offset);
	}
	// Return # of bytes actually written
	return buf_offset;
}	

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */

	// Return -1 if no FS is currently mounted
	if (block_disk_count() == -1)
		return -1;

	// fd out of bounds
	if ((fd > FS_OPEN_MAX_COUNT) || (fd < 0))
		return -1;

	// Return -1 if fd is not currently open
	if (!open_file_table[fd])
		return -1;	
	
	// Return -1 if buff is NULL
	if (!buf)
		return -1;

	// access index of rdir with matching filename of desired fd
	int rdir_index = rdir_filename_index((char*)(open_file_table[fd]->Filename));
	// block index is start point, which block to start read from, adjusted by offset
	int block_index = data_block_index_read(fd, rdir_index);
	// return 0 bytes read if beginning of read is past end of file
	if (block_index == -1)
		return 0;

	int buf_offset = 0;
	int bytes_to_read = count; 
	char bounce_buf[BLOCK_SIZE];
	int read_amount = 0;
	int bb_offset = 0;

	while (1) {
		if ((open_file_table[fd]->offset % BLOCK_SIZE) != 0) {
			// case: reading a block at a offset unaligned with (start of a block)
			if (((open_file_table[fd]->offset + bytes_to_read) % BLOCK_SIZE) != 0) {
				// sub-case: read to offset + bytes_to_read
				read_amount = bytes_to_read;
			} else {
				// sub-case: read rest of block
				read_amount = (BLOCK_SIZE - (open_file_table[fd]->offset % BLOCK_SIZE));
			}
			bb_offset = open_file_table[fd]->offset % BLOCK_SIZE;
		} else if (bytes_to_read < BLOCK_SIZE) {
			// case: read less than a block (end situations)
			read_amount = bytes_to_read;
			bb_offset = 0;
		} else {
			// case: read whole blocks
			read_amount = BLOCK_SIZE;
			bb_offset = 0;
		}
		// read from block into bounce buffer
		block_read(block_index, bounce_buf);
		// copy data from bounce buffer to buf
		memcpy(buf + buf_offset, bounce_buf + bb_offset, read_amount);
		open_file_table[fd]->offset += read_amount;
		bytes_to_read -= read_amount;
		buf_offset += read_amount;

		if (bytes_to_read == 0)
			break;

		if (FAT[block_index] == FAT_EOC) {
			// case: reached end of file, break
			break;
		} else {
			// case: continue traversing through file
			block_index = FAT[block_index];
		}
	}
	
	// Return # of bytes actually read
	return buf_offset;
}

int fat_free_count() {
	int fat_free_count = 0;
	for (uint16_t i = 1; i < superblock->datab_amount; i++) {
		if (FAT[i] == 0)
			fat_free_count++;
	}
	return fat_free_count;
}

int fat_free_index() {
	for (uint16_t i = 1; i < superblock->datab_amount; i++) {
		if (FAT[i] == 0)
			return (int)i;
	}
	return -1;
}

int rdir_free_count() {
	int rdir_free_count = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (root_directory[i]->Filename[0] == '\0')
			rdir_free_count++;
	}
	return rdir_free_count;
}

int rdir_free_index() {
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (root_directory[i]->Filename[0] == '\0')
			return i;
	}
	return -1;
}

int rdir_filename_index(const char *filename) {
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strncmp((char*)(root_directory[i]->Filename), filename, strlen(filename)) == 0)
			return i;
	}
	return -1;
}

int OFT_free_index() {
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
		if (!open_file_table[i])
			return i;
	}
	return -1;
}

int OFT_free_count() {
	int count = 0;
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
		if (!open_file_table[i])
			count++;
	}
	return count;
}

int OFT_filename_index(const char *filename) {
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
		if (open_file_table[i]) {
			if (strncmp((char*)(open_file_table[i]->Filename), filename, strlen(filename)) == 0)
				return i;
		}
	}
	return -1;
}

void memfree_rdir(int max_index) {
	for (int i = 0; i < max_index; i++) {
		free(root_directory[i]);
		root_directory[i] = NULL;
	}
}

int data_block_index_write(int fd, int rdir_index, size_t count) {
	// calculate how many additional data blocks the offset extends to
	int additional_blocks = open_file_table[fd]->offset / BLOCK_SIZE;

	// traverse FAT blocks 
	int start_block_index = (int)(root_directory[rdir_index]->Fdb_index);
	// Return index of free fat data block if first data block is FAT_EOC
	if (count > 0) {
		if (start_block_index == FAT_EOC) {
			start_block_index = fat_free_index();
			if (start_block_index == -1)
				return -1;
			root_directory[rdir_index]->Fdb_index = start_block_index;
		}
	}
	for (int i = 0; i < additional_blocks; i++) {
		uint16_t temp_index = start_block_index;
		start_block_index = FAT[start_block_index];
		if (start_block_index == FAT_EOC) {
			start_block_index = fat_free_index();
			if (start_block_index == -1)
				return -1;

			FAT[temp_index] = start_block_index;
		}
	}
	// datablocks = super + fat + rdir + startpoint
	return start_block_index + (superblock->bs_index);
}

int data_block_index_read(int fd, int rdir_index) {
	// calculate how many additional data blocks the offset extends to
	int additional_blocks = open_file_table[fd]->offset / BLOCK_SIZE;
	// traverse FAT blocks 
	int start_block_index = (int)(root_directory[rdir_index]->Fdb_index);
	for (int i = 0; i < additional_blocks; i++) {
		start_block_index = FAT[start_block_index];
		// Offset starts at beyond end of file, return -1
		if (start_block_index == FAT_EOC) 
			return -1;
	}
	return start_block_index + (superblock->bs_index);
}

int num_blocks(int rdir_index) {
	uint16_t index = root_directory[rdir_index]->Fdb_index;
	if (index == FAT_EOC && root_directory[rdir_index]->Filesize == 0)
		return 0;
	int count = 1;
	while(index != FAT_EOC) {
		index = FAT[index];
		count++;
	}
	return count; 
}