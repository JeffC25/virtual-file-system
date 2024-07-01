#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "fs.h"
#include "disk.h"

#define SIZE 1000
int main()
{
    char *disk_name = "mydisk";
    char *file_name = "myfile";

    assert(make_fs(disk_name) == 0);
    assert(mount_fs(disk_name) == 0);

    assert(fs_create(file_name) == 0);

    int fd;
    fd = fs_open(file_name);
    assert(fd > -1);

    char *buffer1[SIZE];
    for (int i = 0; i < SIZE; i++)
    {
        buffer1[i] = "a";
    }

    assert(fs_write(fd, buffer1, sizeof(buffer1)) == sizeof(buffer1));

    assert(fs_lseek(fd, 0) == 0);

    char *buffer2[SIZE];
    for (int i = 0; i < SIZE; i++)
    {
        buffer2[i] = "b";
    }
    assert(fs_read(fd, buffer2, sizeof(buffer2)) == sizeof(buffer2));

    for (int i = 0; i < SIZE; i++)
    {
        assert(strcmp(buffer1[i], buffer2[i]) == 0);
    }

    assert(fs_close(fd) == 0);

    assert(fs_delete(file_name) == 0);

    assert(umount_fs(disk_name) == 0);

    return 0;
}