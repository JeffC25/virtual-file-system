#include "fs.h"
#include "disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define MAX_FILES_ALLOWED 64    // max 64 files at any given time
#define MAX_F_NAME 15   // max 15 character filenames
#define MAX_FILDES 32   // support a maximum of 32 file descriptors that can be open simultaneously
#define STORAGE 4096 * 4096 // maximum file size is 16M (4,096 blocks, each 4K)
#define DISK_BLOCKS 8192
#define BLOCK_SIZE 4096

// enumeration for file allocation table entries
#define FREE -1         // empty slot in FAT
#define END_MARKER -2   // denote end of file 

// super block to store information of other data structures
struct super_block {
    int fat_idx; // First block of the FAT
    int fat_len; // Length of FAT in blocks
    int dir_idx; // First block of directory
    int dir_len; // Length of directory in blocks
    int data_idx; // First block of file-data
};

// directory entry to stores file metadata
struct dir_entry {
    int used; // Is this file-”slot” in use
    char name [MAX_F_NAME + 1]; // DOH!
    int size; // file size
    int head; // first data block of file
    int ref_cnt;
    // how many open file descriptors are there?
    // ref_cnt > 0 -> cannot delete file
};

// file descriptor used for file operations -- only meaningful while system is mounted
struct file_descriptor {
    int used; // fildes in use
    int file; // the first block of the file (f) to which fildes refers too
    int offset; // position of fildes within f
};

// global variables and data structures
struct super_block *fs; // super block
struct file_descriptor fildes_array[MAX_FILDES]; // array of 32 file descriptors
int *FAT;               // to be populated with the FAT data
struct dir_entry *DIR;  // to be populated with the directory data

int file_counter = 0;   // number of files in system
int mounted = 0;        // if file system has been mounted
int validfs = 0;        // if valid file system has been created

// create a fresh (and empty) file system on the virtual disk
int make_fs(char* disk_name) {
    // make and open virtual disk, return -1 on error
    if(make_disk(disk_name) == -1){
        return -1;
    }
    if(open_disk(disk_name) == -1){
        return -1;
    }

    // initialize superblock
    fs = calloc(1, sizeof(struct super_block));
    fs->fat_idx = 1;    
    fs->fat_len = 4;
    fs->dir_idx = fs->fat_len + fs->fat_idx;
    fs->dir_len = 1;    
    fs->data_idx = fs->dir_len + fs->dir_idx;

    // initialize file allocation table
    FAT = calloc(DISK_BLOCKS, sizeof(int));
    int i;

    for (i = 0; i < DISK_BLOCKS; i++) {
        FAT[i] = FREE;
    }
    for (i = 0; i < (fs->fat_len); i++) {
        if (block_write(i + fs->fat_idx, (char *) (FAT + i *  BLOCK_SIZE)) == -1) {
            return -1;
        }
    }
    
    // initialize directory table
    DIR = calloc(MAX_FILES_ALLOWED, sizeof(struct dir_entry));
    if (block_write(0, (char*) fs) == -1) {
        return -1;
    }
    if (block_write(fs->dir_idx, (char*) DIR) == -1) {
        return -1;
    }

    // ready to mount
    validfs = 1;    
    mounted = 0;

    return close_disk(disk_name);
}

// mount file system stored on virtual disk
int mount_fs(char *disk_name) {
    // check if disk is available to mount
    if (mounted || !validfs) {
        return -1;
    }

    // open disk
    if (open_disk(disk_name)) {
        return -1;
    }

    // read super block info
    if (block_read(0, (char*) fs) == -1) {
        return -1;
    }   

    // read FAT info
    int i;
    for (i = 0; i < (fs->fat_len); i++) {
        if (block_read(i + fs->fat_idx, (char*) (FAT + i * BLOCK_SIZE)) == -1) {
            return -1;
        }
    }

    // read directory info
    if (block_read(fs->dir_idx, (char*) DIR) == -1) {
        return -1;
    }

    // initialize reference count of file descriptor entries
    for (i = 0; i < MAX_FILES_ALLOWED; i++) {
        DIR[i].ref_cnt = 0;
    }

    // initialize file descriptors
    for (i = 0; i < MAX_FILDES; i++) {
        fildes_array[i].used = 0;
        fildes_array[i].file = FREE;
		fildes_array[i].offset = 0;
    }

    mounted = 1;
    return 0;
}

// unmounts file system from virtual disk
int umount_fs(char *disk_name) {
    // check if mounted
    if (!mounted) {
        return -1;
    }

    // write super block info
    if (block_write(0, (char*) fs) == -1) {
        return -1;
    }
    
    // write FAT info
    int i;
    for (i = 0; i < (fs->fat_len); i++) {
        if (block_write(i + (fs->fat_idx), (char*) (FAT + i * BLOCK_SIZE))) {
            return -1;
        }
    }

    // write directory info
    if (block_write(fs->dir_idx, (char*) DIR) == -1) {
        return -1;
    }

    // file descripters no longer meaningful after umount
    for (i = 0; i < MAX_FILDES; i++) {
        fildes_array[i].used = 0;
        fildes_array[i].file = FREE;
        fildes_array[i].offset = 0;
    }

    // close disk
    if (close_disk(disk_name) == -1) {
        return -1;
    }

    // no longer mounted
    mounted = 0;
    
    return 0;
}

// open file for reading and writing
int fs_open(char *name) {
    // return if no files exist
    if (file_counter == 0) {
        return -1;
    }

    int i;
    for (i = 0; i < MAX_FILES_ALLOWED; i++) {
        // check if file exists
        if (strcmp(DIR[i].name, name) == 0) {
            break;
        }
        // check if multiple files reached
        if (i >= MAX_FILES_ALLOWED - 1) {
            return -1;
        }
    }

    // find available file descriptor to assign to file
    int j;
    for (j = 0; j < MAX_FILDES; j++) {
        if (fildes_array[j].used == 0) {
            fildes_array[j].used = 1;
            fildes_array[j].file = DIR[i].head;
            fildes_array[j].offset = 0;
            DIR[i].ref_cnt++;
            return j;
        }
    }

    // return error if no available descriptors
    return -1;
}

// close file specified by file descriptor
int fs_close(int fildes) {
    if (fildes >= MAX_FILDES || fildes < 0) {
        return -1;
    }

    // locate file
    int i;
    for (i = 0; i < MAX_FILES_ALLOWED; i++) {
        if (DIR[i].head == fildes_array[fildes].file && fildes_array[fildes].used) {
            DIR[i].ref_cnt--;
            fildes_array[fildes].used = 0;
            fildes_array[fildes].file = FREE;
            fildes_array[fildes].offset = 0;
            return 0;
        }
    }

    // return error if file not found
    return -1;
}

// create new file in root directory
int fs_create(char *name) {

    // check if maximum files reached
    if (file_counter >= MAX_FILES_ALLOWED) {
        return -1;
    }
    
    // check length of file name
    if (strlen(name) > MAX_F_NAME) {
        return -1;
    }

    // check if file already exists
    int i;
    for (i = 0; i < MAX_FILES_ALLOWED; i++) {
        if (DIR[i].used && strcmp(DIR[i].name, name) == 0) {
            return -1;
        }
    }

    // locate available slot in FAT
    for (i = fs->data_idx; i < DISK_BLOCKS; i++) {
        if (FAT[i] == FREE) {
            FAT[i] = END_MARKER;
            break;
        }
        // return error if no available slots
        if (i >= DISK_BLOCKS) {
            return -1;
        }
    }

    // locate available slot in directory
    int j;
    for (j = 0; j < MAX_FILES_ALLOWED; j++) {
        if (!DIR[j].used) {
            file_counter++;
            DIR[j].used = 1;
            DIR[j].size = 0;
            DIR[j].head = i;
            DIR[j].ref_cnt = 0;
            strcpy(DIR[j].name, name);
            fs->dir_len++;
            return 0;
        }
    }

    // return error if no available slots
    return -1;
}

// delete file from root directory
int fs_delete(char *name) {
    // check if name valid
    if (strlen(name) > MAX_F_NAME) {
        return -1;
    }

    // locate file
    int i;
    for (i = 0; i < MAX_FILES_ALLOWED; i++) {
        // check name and reference counter (can not delete if reference counter < 0)
        if (strcmp(DIR[i].name, name) == 0 && DIR[i].ref_cnt == 0) {
            int next;
            int block = DIR[i].head;
            while (FAT[DIR[i].head] != FREE) {
                next = FAT[block];
                FAT[block] = FREE;
                if (next == END_MARKER) {
                    break;
                }
                block = next;
            }
            // update directory entry
            DIR[i].used = 0;
            DIR[i].size = 0;
            DIR[i].head = FREE;
            memset(DIR[i].name, '\0', strlen(DIR[i].name));

            // update directory length and file counter
            fs->dir_len--;
            file_counter--;
            return 0;
        }
    }

    // return error if file not found
    return -1;
}

// read nbytes of data into buffer
int fs_read(int fildes, void *buf, size_t nbyte) {
    // check if file descriptor and nbyte input are valid    
    if (fildes > MAX_FILDES || fildes < 0 || nbyte < 0) {
        return -1;
    }
    if (!fildes_array[fildes].used) {
        return -1;
    }
    if (nbyte == 0) {
        return 0;
    }

    // locate file
    int entry;
    for (entry = 0; entry < MAX_FILES_ALLOWED; entry++) {
        if (fildes_array[fildes].file == DIR[entry].head) {
            break;
        }
        // return error if file not found
        if (entry >= MAX_FILES_ALLOWED - 1) {
            return -1;
        }
    }

    // update bytes to read if needed
    if (nbyte + fildes_array[fildes].offset > DIR[entry].size) {
        nbyte = DIR[entry].size - fildes_array[fildes].offset;
    }

    // allocate character buffers
    char blocks[BLOCK_SIZE];
    memset(blocks, FREE, BLOCK_SIZE);
    char* buffer = (char *) calloc(1, nbyte * sizeof(char));
    memset(buffer, 0, nbyte);
    int offset = fildes_array[fildes].offset;
    int block = fildes_array[fildes].file;

    // go to first block
	while(offset >= BLOCK_SIZE){
		offset -= BLOCK_SIZE;
		block = FAT[block];
	}

    int remaining = nbyte;
    int reading = 0;
    while (remaining > 0) {
        // read into buffer
        block_read(block, blocks);
        int i;
        for (i = offset; i < BLOCK_SIZE; i++) {
            buffer[reading] = blocks[i];
            reading++;
            remaining--;
            // stop loop once all bytes read
            if (!remaining) {
                break;
            }
        }
        // go to next block if still reading
        if (remaining) {
            block = FAT[block];
            offset = 0;
        }
    }

    // copy into input buffer
    memcpy(buf, buffer, nbyte);
    
    // return number of bytes read
    return nbyte;
}

// write nbytes of data from buffer
int fs_write(int fildes, void *buf, size_t nbyte) {
    int bytes_written = 0;
    int remaining;

    // check if file descriptor and nbyte input are valid
    if (fildes > MAX_FILDES || fildes < 0 || nbyte < 0) {
        return -1;
    }
    if (!fildes_array[fildes].used) {
        return -1;
    }
    if (nbyte == 0) {
        return 0;
    }

    // locate file
    int entry;
    for (entry = 0; entry < MAX_FILES_ALLOWED; entry++) {
        if (fildes_array[fildes].file == DIR[entry].head) {
            break;
        }
        // return error if file not found
        if (entry >= MAX_FILES_ALLOWED - 1) {
            return -1;
        }
    }

    // if read will exceed storage space --> update nbyte
    if (nbyte + fildes_array[fildes].offset > STORAGE) {
        nbyte = STORAGE - fildes_array[fildes].offset;
    }

    // bytes to write
    remaining = nbyte;

    // allocate buffers
    char blocks[BLOCK_SIZE];
    memset(blocks, FREE, BLOCK_SIZE);

    int block = fildes_array[fildes].file;
    int prev = 0;
    int offset = fildes_array[fildes].offset;
    while (offset >= BLOCK_SIZE) {
        offset -= BLOCK_SIZE;
        prev = block;
        block = FAT[block];
    }

    while (remaining > 0) {
        // update eof marker
        if (block == END_MARKER) {
            int j;
            for (j = fs->data_idx; j < DISK_BLOCKS; j++) {
                if (FAT[j] == FREE) {
                    FAT[j] = END_MARKER;
                    FAT[prev] = j;
                    block = FAT[prev];
                    break;
                }
            }
        }

        if (block_read(block, blocks) == -1) {
            return -1;
        }

        // write to buffer
        int i;
        for (i = offset; i < BLOCK_SIZE; i++) {
            blocks[i] = *((char *) buf + bytes_written);
            bytes_written++;
            fildes_array[fildes].offset++;
            remaining--;
            if (!remaining) {
                break;
            }
            if (i + offset > STORAGE) {
                return bytes_written;
            }
        }
        // go to next block if still writing
        if (remaining) {
            if (block_write(block, blocks) == -1) {
                return -1;
            }
            prev = block;
            block = FAT[block];
            offset = 0;
        }
    }

    // load block
    if (block_write(block, blocks) == -1) {
        return -1;
    }

    // update file size
    if (DIR[entry].size < fildes_array[fildes].offset) {
        DIR[entry].size = fildes_array[fildes].offset;
    }   

    // return number of bytes written
    return bytes_written;
}

// return current size of file
int fs_get_filesize(int fildes) {
    // out of range
    if (fildes >= MAX_FILDES || fildes < 0) {
        return -1;
    }

    int i;
    for (i = 0; i < MAX_FILES_ALLOWED; i++) {
        if (DIR[i].head == fildes_array[fildes].file) {
            if (!fildes_array[fildes].used) {
                return -1;
            }
            return DIR[i].size;
        }
    }

    return -1;
}

// creates and populates array of file names currently known to file system
int fs_listfiles(char ***files) {
    // allocate new list
    char** list = calloc(MAX_FILES_ALLOWED, sizeof(char*));

    // locate in-use entries
    int i;
    int j = 0;
    for (i = 0; i < MAX_FILES_ALLOWED; i++) {
        if (DIR[i].used) {
            // append entry name to list
            *(list + j) = DIR[i].name;
            j++;
        }
    }
    // terminate array
    *(list + j) = NULL;

    // update input pointer
    *files = list;

    return 0;   
}

// sets file pointer (offset used for read and write operations)
int fs_lseek(int fildes, off_t offset) {
    int file_size = fs_get_filesize(fildes);

    // out of range
    if (offset > file_size || offset < 0) {
        return -1;
    }

    // invalid fildes
    if (fildes > MAX_FILDES || fildes < 0) { 
        return -1;
    }

    // invalid fildes
    if (!fildes_array[fildes].used) {
        return -1;
    }
    
    // update offset
    fildes_array[fildes].offset = offset;

    return 0;
}

// truncate file to (length) bytes in size
int fs_truncate(int fildes, off_t length) {
    // out of range
    if (fildes >= MAX_FILDES || fildes < 0 || length > STORAGE || length < 0) {
        return -1;
    }

    // file descriptor not in use
    if (!fildes_array[fildes].used) {
        return -1;
    }

    int i;
    for (i = 0; i < MAX_FILES_ALLOWED; i++) {
        // locate directory entry
        if (DIR[i].head == fildes_array[fildes].file) {

            // check if entry size already smaller than truncation length
            if (DIR[i].size < length) {
                return -1;
            }
            // if entry size same as truncation length --> do nothing
            else if (DIR[i].size == length) {
                return 0;
            }

            // update file descriptor offset
            if (fildes_array[fildes].offset > length) {
                fildes_array[fildes].offset = length;
            }

            // go to new boundary block
            int block = DIR[i].head;
            int j;
            for (j = 0; j < length / BLOCK_SIZE; j++) {
                block = FAT[block];
            }
            int last = block;
            int traverse = FAT[block];

            // free truncated blocks
            while (traverse > 0) {
                traverse = FAT[traverse];
                FAT[traverse] = FREE;
            }

            // mark end
            FAT[last] = END_MARKER;

            // update entry size
            DIR[i].size = length;
            
            return 0;
        }
    }

    return -1;   
}
