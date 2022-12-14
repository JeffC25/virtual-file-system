#ifndef FS_H
#define FS_H

#include <stdio.h>
#include "disk.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int make_fs(char* diskname);

int mount_fs(char *disk_name);

int umount_fs(char *disk_name);

int fs_open(char *name);

int fs_close(int fd);

int fs_create(char *name);

int fs_delete(char *name);

int fs_read(int fd, void *buf, size_t nbyte);

int fs_write(int fd, void *buf, size_t nbyte);

int fs_get_filesize(int fd);

int fs_listfiles(char ***files);

int fs_lseek(int fd, off_t offset);

int fs_truncate(int fd, off_t length);

#endif
