#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/fs.h>
#include <unistd.h>
#include <openssl/sha.h>

#include "../ransomfs.h"

void main() {

    int ret;

    ret = mknod("/tmp/mnt/ms_test2", S_IFREG, 0);
    printf("%d\n", ret);

    int fd = open("/tmp/mnt/ms_test2", O_WRONLY);

    ret = write(fd, "testtest", 9);
    printf("%d\n", ret);
    ret = lseek(fd, 0, SEEK_SET);
    printf("%d\n", ret);
    ret = write(fd, "changed", 8);
    printf("%d\n", ret);

    close(fd);
}