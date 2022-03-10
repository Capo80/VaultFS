# FS Protection - Implementation Details

This Module implements 3 protective measures that, when active, have the following effects:
1. Write protection on file, files should only be written once and never modified;
2. Umount protection, the filesystem should be detached only on system shutdown;
3. Underlaying block device protection, the user should not be able to interact with the underlying block device after the system is mounted;

Protection 2 can be activated/deactivated with the device called controller by using a single unique, secure password.

All protections are active by default at Filesystem Mounting. 

## Password persistance

The password that is used to umount is saved i nthe File System Superblock and is choosen during initial formatting.

We extract the password from the superblock by using the data paramenter of the mount syscall in the "vaultfs_mount" function, it could also be possible to change the password at this point if the user passes it as a parameter, this is not yet implemented.

## Umount protection

We pair the mount point with the password during in the "vaultfs_mount" function.

When an umount is called we intercept it in the "security_sb_umount" function with a kprobe and block it if the mount point is the one we saved.

We don't need to worry about symlinks or mount namespaces, at this point thoose have already been translated by the kernel.

## Block Device protection

To write on the Block Device the FileSystem keeps a block_device structure inside his superblock and calls the block device operations directly using the buffer_head API to implement async operations and caching, this means we cannot destroy the linking of the operations or the FileSystem will not be able to interact with the device.

I believe tough that the only way for a user space process to be able to interact directly with the block device is with the open syscall, beacuse of this we can use a probe on the "security_file_open", check if the path opened is a block device and block the open if it is the one that the FileSystem is using.

We can pair the device with the password again in the "vaultfs_mount" function.

To recognize the block device we should be able to use the Major/Minor number (i think?).

## Write protection

There are 2 types of write protection:
- Single session, the only session that is able to write on the file is the first one;
- Append only, the writes on the file cannot overwrite exisitng data;

A file of VaultFS must have at least one of the 2 and, by default, will have both. Files with different level of protection can coexist on the same instance of the file system.

Single session is implemented in the the "open" operation, we keep additional information in the inode to check if it has already been written and deny the open in WRITE MODE if it is.

Append only in the "write_begin" implementation, we just need to check that the write happens after the current end of the file.







