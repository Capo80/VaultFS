#include "ransomfs_syscalls.h"

#include "lib/usctm.h"

// ########## helpers ########################

unsigned long cr0;

inline void write_cr0_forced(unsigned long val)
{
    unsigned long __force_order;

    /* __asm__ __volatile__( */
    asm volatile(
        "mov %0, %%cr0"
        : "+r"(val), "+m"(__force_order));
}

inline void protect_memory(void)
{
    write_cr0_forced(cr0);
}

inline void unprotect_memory(void)
{
    write_cr0_forced(cr0 & ~X86_CR0_WP);
}

// ############ syscalls #########################

//syscall to control the umount protection
__SYSCALL_DEFINEx(2, _umount_ctl, umount_security_info_t* , info, int, command){
    
    struct ransomfs_security_info* cur;
    int ret = 0;
    umount_security_info_t* k_info = kzalloc(sizeof(umount_security_info_t), GFP_KERNEL);
    AUDIT(TRACE)
    printk(KERN_INFO"umount ctl called, command: %d\n", command);

    __copy_from_user(k_info, info, sizeof(umount_security_info_t));

    //TODO - do i need to do some locking here? i think not
    switch (command)
    {
    case UMOUNT_LOCK:
        list_for_each_entry(cur, &security_info_list, node) {
            if (strcmp(cur->mount_path, k_info->mount_point) == 0) {
                if (strcmp(cur->password_hash, k_info->password) == 0) { //TODO hash here
                    AUDIT(TRACE)
                    printk(KERN_INFO "Password is corrent, umount locked\n");
                    cur->umount_lock = 1;
                    ret = 0;
                } else
                    ret = -EINVAL;
                break;
            }
        }
        break;
    case UMOUNT_UNLOCK:
        list_for_each_entry(cur, &security_info_list, node) {
            if (strcmp(cur->mount_path, k_info->mount_point) == 0) {
                if (strcmp(cur->password_hash, k_info->password) == 0) { //TODO hash here
                    AUDIT(TRACE)
                    printk(KERN_INFO "Password is corrent, umount unlocked\n");
                    cur->umount_lock = 0;
                    ret = 0;
                } else
                    ret = -EINVAL;
                break;
            }
        }
        break;
    default:                    
        AUDIT(ERROR)
        printk(KERN_INFO "Invalid command\n");
        ret = -EINVAL;
        break;
    }
    
    kfree(k_info);
    return ret;
}

int insert_syscalls() {

    //find syscall table
	syscall_table_finder();

	if(!hacked_syscall_tbl){
		AUDIT(ERROR)
        printk(KERN_ERR"failed to find the sys_call_table\n");
		return -1;
	}

    //add new syscall
	cr0 = read_cr0();
    unprotect_memory();
    hacked_syscall_tbl[FIRST_NI_SYSCALL] = (unsigned long*) __x64_sys_umount_ctl;
    protect_memory();
    
	AUDIT(TRACE)
    printk(KERN_INFO"umount_ctl installed on the sys_call_table at displacement %d\n", FIRST_NI_SYSCALL);	

    return 0;

}


int remove_syscalls() {

    if(!hacked_syscall_tbl){
        AUDIT(TRACE)
        printk("failed to find the sys_call_table\n");
        return -1;
    }

    //put back sys_ni_syscall
    cr0 = read_cr0();
    unprotect_memory();
    hacked_syscall_tbl[FIRST_NI_SYSCALL] = (unsigned long*) hacked_syscall_tbl[SEVENTH_NI_SYSCALL];
    protect_memory();
    
    AUDIT(TRACE)
    printk(KERN_INFO"syscalls correctly unistalled\n"); 

    return 0;
    
}