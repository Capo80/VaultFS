#ifndef SYSCALLS_HELP_H
#define SYSCALLS_HELP_H

#define FIRST_NI_SYSCALL	134
#define SECOND_NI_SYSCALL	174
#define THIRD_NI_SYSCALL	182 
#define FOURTH_NI_SYSCALL	183
#define FIFTH_NI_SYSCALL	214	
#define SIXTH_NI_SYSCALL	215	
#define SEVENTH_NI_SYSCALL	236	

unsigned long *hacked_ni_syscall = NULL;
unsigned long **hacked_syscall_tbl = NULL;
unsigned long sys_call_table_address = 0x0;
unsigned long sys_ni_syscall_address = 0x0;

void syscall_table_finder(void);

#endif