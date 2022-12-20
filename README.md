# virtual-file-system

int make_fs(char* diskname);
        This function creates an empty file system on the virtual disk and initializes the superblock, file allocation table, and file directory.

int mount_fs(char *disk_name);
        This function mounts the file system stored on the virtual disk. It first checks that the system has not yet been mounted and that a valid file system has been created. It then opens the specified disk and calls block_read to load in the metadata into the appropriate data structures. Lastly, resets all the values in the global file directory array.

int umount_fs(char *disk_name);
        This function unmounts the file system. It first checks that the system is currently mounted and then calls block_write to write metadata from the appropriate data structures onto their respective locations on disk, before resetting the global file directory array.

int fs_open(char *name);
        This function opens a specified file for reading. It locates the file from the directory and then locates an available slot in the file descriptor array. If a slot is available, it is populated, and the reference counter of the file in its directory entry is incremented.

int fs_close(int fildes);
        This function closes a currently open file. After locating the file from the directory, it frees the corresponding slot in the file descriptor array and decrements the reference count.

int fs_create(char *name);
        This function creates a new file on the disk. It checks that the specified file name does not already exist, that the specified name does not exceed the maximum characters allowed, and that the current global file counter is not at capacity. Next, it locates an available slot in the file allocation table to mark as EOF, along with an available slot in the directory to populate. Lastly, it increments the global file counter and directory length stored in the superblock.

int fs_delete(char *name);
        This function deletes a file from the disk. It locates the file from the directory and traverses the file allocation table to free all allocated slots. The directory entry is also freed, and both the global file counter and directory length specified in the superblock are decremented.

int fs_read(int fildes, void *buf, size_t nbyte);
        This function reads a nbytes of data into a buffer. It first checks that the specified file descriptor is valid and locates the file in the directory.  Next, it allocates a buffer of characters to read into and traverses the file allocation table to the first block of the file. It then reads from the block into the buffer, traversing to the next block if there are bytes left to read. Lastly, it copies the data in the buffer into the specified input buffer and returns the number of bytes read.

int fs_write(int fildes, void *buf, size_t nbyte);
        This function writes nbytes of data into a file from a buffer. It first checks that the specified file descriptor is valid and locates the file in the directory.  Next, it allocates a buffer of characters to read into and traverses the file allocation table to the first block of the file. It then reads the data from the input buffer into the new buffer and then writes the new buffer data into the block pertaining to the file, continuously traversing to the next block if necessary. Finally, it updates directory entry size for the file and returns the number of bytes written

int fs_get_filesize(int fildes);
        This function returns the size of the file specified by a file descriptor. It checks that the descriptor is valid and then locates a file in the directory whose first block is the same as that of the file specified by the file descriptor, before returning the size data.

int fs_listfiles(char ***files);
        This file creates and populates an array of file names currently in the system. It first allocates a list of character pointers and traverses the directory to locate any in-use entries. If such entries are found, it points the next element in the array to the file name specified by the directory entry. Lastly, it sets the last array element to NULL and updates the input pointer to refer to the array.

int fs_lseek(int fildes, off_t offset);
        This function updates a file location offset. It verifies that the specified file descriptor is valid and then sets the offset of the corresponding entry in the file allocation table to the specified offset.

int fs_truncate(int fildes, off_t length);
        This function truncates a file to a specified number of bytes in size. It first checks that  the offset and specified length are valid. Next, it locates the file in the directory and also updates the offset field in the appropriate file allocation table entry. It then traverses the FAT and frees all blocks that are to be trimmed off. Lastly, it updates the EOF block of the file in the FAT and updates the size field of the file in its directory entry.
