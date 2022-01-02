#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "../syscalls/ransomfs_syscalls.h"

void main() {
    umount_security_info_t info = {
        .password = "1234",
        .mount_point = "/home/capo80/Desktop/RansomFS/src/mnt",
    };
    umount_ctl(&info, UMOUNT_UNLOCK);
}
