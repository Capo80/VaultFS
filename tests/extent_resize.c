#include<stdio.h>
#include<limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

//arg[1] is Mount path - /tmp/mnt if empty
int main(int argc, char **argv) {

    char file1_path[PATH_MAX], file2_path[PATH_MAX], buffer1[4500], buffer2[4500];
    int file1, file2, ret1, ret2;

    //get path
    if (argc < 2) {
        strcpy(file1_path, "/tmp/mnt/file1");
        strcpy(file2_path, "/tmp/mnt/file2");
    } else {
        sprintf(file1_path, "%s/%s", argv[1], "file1");
        sprintf(file2_path, "%s/%s", argv[1], "file2");
    }

    file1 = open(file1_path, O_CREAT | O_WRONLY);
    file2 = open(file2_path, O_CREAT | O_WRONLY);
    if (file1 < 0 || file2 < 0) {
        printf("Cannot create file\n");
        return -1;
    }

    //fill buffer with recognizable characters
    for (int i = 0; i < sizeof(buffer1); i++) buffer1[i] = 'A';
    for (int i = 0; i < sizeof(buffer2); i++) buffer2[i] = 'B';

    //we need to write ~10 non-consecutive blocks to get the extent to resize
    for (int i = 0; i < 15; i++) 
    {
        ret1 = write(file1, buffer1, sizeof(buffer2));
        ret2 = write(file2, buffer2, sizeof(buffer2));
        if (ret1 != sizeof(buffer1) || ret2 != sizeof(buffer2)) {
            printf("Cannot write on file");
            return -1;
        }
    }

    close(file1);
    close(file2);
    return 0;
}