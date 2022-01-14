#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../syscalls/ransomfs_syscalls.h"

int main(int argc, char **argv) {    
    
    if (argc != 3) {
        fprintf(stderr, "Usage: %s mount_path mount_password\n", argv[0]);
        return EXIT_FAILURE;
    }
    umount_security_info_t info;
    memcpy(info.mount_point, argv[1], strlen(argv[1])+1);
    memcpy(info.password, argv[2], strlen(argv[2])+1);

    printf("Calling umount_ctl(\"%s\",\"%s\", UMOUNT_UNLOCK)...\n", info.mount_point, info.password);
    return umount_ctl(&info, UMOUNT_UNLOCK);

}
