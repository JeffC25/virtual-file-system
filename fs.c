#include "fs.h"
#include "disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define MAX_FILES_ALLOWED 64    // "Max 64 files at any given time"
#define MAX_F_NAME 15   // "Max 15 character filenames"
#define MAX_FILDES 32   // "Your library must support a maximum of 32 file descriptors that can be open simultaneously"
#define STORAGE 4096 * 4096 * 2 // "The maximum file size is 16M (which is, 4,096 blocks, each 4K)"
#define DISK_BLOCKS 8192
#define BLOCK_SIZE 4096

// enumeration for file allocation table entries
#define FREE -1         // empty slot in FAT
#define END_MARKER -2   // denote end of file 
#define RESERVED -3     // reserved FAT entry

//  data structures
struct super_block {
    int fat_idx; // First block of the FAT
    int fat_len; // Length of FAT in blocks
    int dir_idx; // First block of directory
    int dir_len; // Length of directory in blocks
    int data_idx; // First block of file-data
};

struct dir_entry {
    int used; // Is this file-”slot” in use
    char name [MAX_F_NAME + 1]; // DOH!
    int size; // file size
    int head; // first data block of file
    int ref_cnt;
    // how many open file descriptors are there?
    // ref_cnt > 0 -> cannot delete file
};

struct file_descriptor {
    int used; // fildes in use
    int file; // the first block of the file (f) to which fildes refers too
    int offset; // position of fildes within f
};

// global variables and data structures
struct super_block *fs;
struct file_descriptor fildes_array[MAX_FILDES]; // 32
int *FAT; // Will be populated with the FAT data
struct dir_entry *DIR; // Will be populated with the directory data

int file_counter = 0;   // number of files
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

    // initialize values for the superblock
    fs = calloc(1, sizeof(struct super_block));
    fs->fat_idx = 1;    // first block of FAT
    fs->fat_len = 4;    // length of FAT in blocks (8192 * 4 / 4098)
    fs->dir_idx = 5;    // first block of directory (4 + 1)
    fs->dir_len = 0;    // length of directory in files
    fs->data_idx = 6;   // first block of file-data (5 + 1)

    // initialize file allocation table
    FAT = calloc(DISK_BLOCKS, sizeof(int));
    int i;
    for (i = 0; i < DISK_BLOCKS; i++) {
        if (i < (fs->fat_len)) {
            FAT[i] = RESERVED;  // reserve blocks 0 to 3 for metadata
        }
        else {
            FAT[i] = FREE;
        }
    }

    DIR = calloc(MAX_FILES_ALLOWED, sizeof(struct dir_entry));

    if (block_write(0, (char*) fs) == -1) {
        return -1;
    }
    if (block_write(fs->dir_idx, (char*) DIR) == -1) {
        return -1;
    }

    for (i = 0; i < (fs->fat_len); i++) {
        if (block_write(i + fs->fat_idx, (char *) (FAT + i *  BLOCK_SIZE)) == -1) {
            return -1;
        }
        
    }
    // ready to mount
    validfs = 1;    
    mounted = 0;

    return close_disk(disk_name);
}

int mount_fs(char *disk_name) {
    if (mounted || !validfs) {
        return -1;
    }

    if (open_disk(disk_name)) {
        return -1;
    }

    if (block_read(0, (char*) fs) == -1) {
        return -1;
    }
        
    int i;
    for (i = 0; i < (fs->fat_len); i++) {
        if (block_read(i + fs->fat_idx, (char*) (FAT + i * BLOCK_SIZE)) == -1) {
            return -1;
        }
        
    }

    if (block_read(fs->dir_idx, (char*) DIR) == -1) {
        return -1;
    }

    for (i = 0; i < MAX_FILES_ALLOWED; i++) {
        DIR[i].ref_cnt = 0;
    }

    for (i = 0; i < MAX_FILDES; i++) {
        fildes_array[i].used = 0;
        fildes_array[i].file = FREE;
		fildes_array[i].offset = 0;
    }

    mounted = 1;
    return 0;
}

int umount_fs(char *disk_name) {

    if (!mounted) {
        return -1;
    }

    if (block_write(0, (char*) fs) == -1) {
        return -1;
    }
    
    int i;
    for (i = 0; i < (fs->fat_len); i++) {
        if (block_write(i + (fs->fat_idx), (char*) (FAT + i * BLOCK_SIZE))) {
            return -1;
        }
    }

    if (block_write(fs->dir_idx, (char*) DIR) == -1) {
        return -1;
    }

    // file descripters no longer meaningful after umount
    for (i = 0; i < MAX_FILDES; i++) {
        // if (fildes_array[i].used) {
        //     fildes_array[i].used = 0;
        //     fildes_array[i].file = FREE;
        //     fildes_array[i].offset = 0;
        // }
        fildes_array[i].used = 0;
        fildes_array[i].file = FREE;
        fildes_array[i].offset = 0;
    }

    mounted = 0;

    if (close_disk(disk_name) == -1) {
        return -1;
    }

    
    return 0;
}

int fs_open(char *name) {

    if (file_counter == 0) {
        return -1;
    }

    int i;
    for (i = 0; i < MAX_FILES_ALLOWED; i++) {
        if (strcmp(DIR[i].name, name) == 0) {
            break;
        }
        if (i >= MAX_FILES_ALLOWED - 1) {
            return -1;
        }
    }

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
    return -1;

}

int fs_close(int fildes) {
    if (fildes >= MAX_FILDES || fildes < 0) {
        return -1;
    }

    int i;
    for (i = 0; i < MAX_FILES_ALLOWED; i++) {
        // file found
        if (DIR[i].head == fildes_array[fildes].file && fildes_array[fildes].used) {
            DIR[i].ref_cnt--;
            fildes_array[fildes].used = 0;
            fildes_array[fildes].file = FREE;
            fildes_array[fildes].offset = 0;
            //file_counter--;
            return 0;
        }
        // file not found
        // if (i >= MAX_FILES_ALLOWED - 1;) {
        //     return -1;
        
    }
    return -1;
}

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

    // 

    for (i = fs->data_idx; i < DISK_BLOCKS; i++) {
        if (FAT[i] == FREE) {
            FAT[i] = END_MARKER;
            break;
        }
        if (i >= DISK_BLOCKS) {
            return -1;
        }
    }

    int j;
    for (j = 0; j < MAX_FILES_ALLOWED; j++) {
        // free slot found
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

    // return -1 if all slots occupied
    return -1;
}

int fs_delete(char *name) {
    if (strlen(name) > MAX_F_NAME) {
        return -1;
    }

    int i;
    for (i = 0; i < MAX_FILES_ALLOWED; i++) {
        // file found, reference count = 0
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
            DIR[i].used = 0;
            DIR[i].size = 0;
            DIR[i].head = FREE;
            //DIR[i].name = '';
            memset(DIR[i].name, '\0', strlen(DIR[i].name)); // + 1
            fs->dir_len--;
            file_counter--;
            return 0;
        }
        // file not found
        // if (i >= MAX_FILES_ALLOWED - 1) {
        //     return -1;
        // }
    }
    return -1;
}

int fs_read(int fildes, void *buf, size_t nbyte) {
    //int bytes_read = 0;
    
    if (fildes > MAX_FILDES || fildes < 0 || nbyte < 0) {
        return -1;
    }
    if (!fildes_array[fildes].used) {
        return -1;
    }

    int entry;
    for (entry = 0; entry < MAX_FILES_ALLOWED; entry++) {
        if (fildes_array[fildes].file == DIR[entry].head) {
            break;
        }
        if (entry >= MAX_FILES_ALLOWED - 1) {
            return -1;
        }
    }

    if (nbyte + fildes_array[fildes].offset > DIR[entry].size) {
        nbyte = DIR[entry].size - fildes_array[fildes].offset;
    }

    // int blocks[BLOCK_SIZE];
    // int i;
    // for (i = 0; i < BLOCK_SIZE; i++) {
    //     blocks[i] = FREE;
    // }
    char blocks[BLOCK_SIZE];
    memset(blocks, FREE, BLOCK_SIZE);

    char* buffer = (char *) calloc(1, nbyte * sizeof(char));
    memset(buffer, 0, nbyte);
    int offset = fildes_array[fildes].offset;
    int block = fildes_array[fildes].file;

    // offset = BLOCK_SIZE % offset;
	while(offset >= BLOCK_SIZE){
		offset -= BLOCK_SIZE;
		block = FAT[block];
	}

    int remaining = nbyte;
    int reading = 0;
    while (remaining > 0) {
        block_read(block, blocks);
        int i;
        for (i = offset; i < BLOCK_SIZE; i++) {
            buffer[reading] = blocks[i];
            reading++;
            remaining--;
            if (!remaining) {
                break;
            }
        }
        if (remaining) {
            block = FAT[block];
            offset = 0;
        }
    }
    memcpy(buf, buffer, nbyte);
    
    return nbyte;
}

int fs_write(int fildes, void *buf, size_t nbyte) {
    int bytes_written = 0;
    int remaining;

    if (fildes > MAX_FILDES || fildes < 0 || nbyte < 0) {
        return -1;
    }
    if (!fildes_array[fildes].used) {
        return -1;
    }
    if (nbyte == 0) {
        return 0;
    }

    int entry;
    for (entry = 0; entry < MAX_FILES_ALLOWED; entry++) {
        if (fildes_array[fildes].file == DIR[entry].head) {
            break;
        }
        if (entry >= MAX_FILES_ALLOWED - 1) {
            return -1;
        }
    }

    if (nbyte + fildes_array[fildes].offset > STORAGE) {
        nbyte = STORAGE - fildes_array[fildes].offset;
    }
    remaining = nbyte;

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
        if (remaining) {
            if (block_write(block, blocks) == -1) {
                return -1;
            }
            prev = block;
            block = FAT[block];
            offset = 0;
        }
    }

    if (block_write(block, blocks) == -1) {
        return -1;
    }
    // DIR[entry].size += bytes_written;
    if (DIR[entry].size < fildes_array[fildes].offset) {
        DIR[entry].size = fildes_array[fildes].offset;
    }   

    return bytes_written;
}

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

int fs_listfiles(char ***files) {

    char** list = calloc(MAX_FILES_ALLOWED, sizeof(char*));

    //printf("list allocated\n");
    
    // if (file_counter == 0) {
    //     *files = list;
    //     return -0;
    // }
    // else if (!mounted || !validfs) {
    //     return -1;
    // }

    //printf("setup done\n");

    int i;
    int j = 0;

    for (i = 0; i < MAX_FILES_ALLOWED; i++) {
        //printf("i: %d\n", i);
        if (DIR[i].used) {
            //printf("Entry name: %s \n", DIR[i].name);
            *(list + j) = DIR[i].name;
            //strcpy(list[j], DIR[i].name);

            //printf("name copied\n");

            j++;
        }
    }
    *(list + j) = NULL; // terminate array

    //printf("list terminated\n");

    *files = list;

    return 0;   
}

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

    if (!fildes_array[fildes].used) {
        return -1;
    }
    
    fildes_array[fildes].offset = offset;

    return 0;
}

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
        if (DIR[i].head == fildes_array[fildes].file) {

            if (DIR[i].size < length) {
                return -1;
            }
            else if (DIR[i].size == length) {
                return 0;
            }

            DIR[i].size = length;
            //fildes_array[i].offset = min(DIR[i].offset, length);
            if (fildes_array[fildes].offset > length) {
                fildes_array[fildes].offset = length;
            }
            
            int block = DIR[i].head;
            int j;
            for (j = 0; j <= length / BLOCK_SIZE; j++) {
                block = FAT[block];
            }
            int last = block;
            int traverse = FAT[block];

            while (traverse > 0) {
                traverse = FAT[traverse];
                FAT[traverse] = FREE;
            }
            FAT[last] = END_MARKER;
            
            return 0;
        }
    }

    return -1;
}
