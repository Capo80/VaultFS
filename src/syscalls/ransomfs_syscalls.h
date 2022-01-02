#ifndef RANSOMFS_SYSCALLS_H
#define RANSOMFS_SYSCALLS_H

#define UMOUNT_LOCK     0x0
#define UMOUNT_UNLOCK   0x1

#define PASSWORD_MAX_SIZE   32 //in bytes
#define PATH_MAX            4096

typedef struct umount_security_info {
    char mount_point[PATH_MAX];
    char password[PASSWORD_MAX_SIZE];
} umount_security_info_t;

#define umount_ctl(info, command) 	        syscall(134, info, command)

#ifdef __KERNEL__  //prevent errors when including in user mode

#include <linux/syscalls.h>
#include "lib/vtpmo.h"

#include "../ransomfs.h"
#include "../ransomfs_security.h"

//helpers
inline void write_cr0_forced(unsigned long val);
inline void protect_memory(void);
inline void unprotect_memory(void);
int insert_syscalls(void);
int remove_syscalls(void);

#endif //__KERNEL__

#endif