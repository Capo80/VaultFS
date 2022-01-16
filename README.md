# RansomFS
A simplified version of the ext4 FileSystem with added security features. This FileSystem is designed for back-ups of highly sensible data and is intended as a defensive measure against Ransomware attacks.

The security features that i plan to implement are:
- Write protection on file, files should only be written once and never modified;
- Umount protection, the filesystem should be detached only on system shutdown;
- Underlaying block device protection, the user should not be able to interact with the underlying block device after the system is mounted;

Details on the implementation are in the [implementation details file](docs/Implemention%20details%20of%20the%20protections.md).

## Disk-Layout

The disk layout is based on the ext4 FileSystem, the disk is divided in groups and each group has the following organization: 

| rfs superblock <br/> (only in group 0) | Group descriptor table <br/> (only in group 0) | Data Block bitmap | Inode Bitamp | Inode Table | Data Blocks |
|:-:|:-:|:-:|:-:|:-:|:-:|
| 1 block | 1 block | 1 block | 1 block | 1024 blocks | 317421 Blocks |

The separation in groups allows us to allocate and find blocks more efficiently and it also makes it esier to avoid fragmentation.

Sizes and limitations are the same of the ext4 FileSystem with a block size of 4 Kb.

The major differences with the ext4 FileSystem are:
- No Reserved GDT blocks, ext4 allocates blocks that can be used to resize the FileSystem if needed, this FileSystem will not have this feature;
- Linear addressing for directories, directory will be treated as linear arrays, not as hash trees;
- No group 0 padding, the ext4 FileSystem leaves 1024 bytes padding at the start of the for boot sectors and other operating system oddities, this FileSytem is not maent to house an OS so this is not needed;
- No journaling, definitely something that should be implemented, but i believe it is out of the scope of the project for now.
- Fixed block size of 4Kb;
- No 32-bit mode for addressing;

## Build

This module has been written and tested on kernel 5.8, different kernel versions are not garanteed to work (and probably won't).

### The hard way

Enter the src folder and run: ```make```

The MakeFile will create a RansomFS formatted test image in the current folder name ```test.img```

Default size for the test image is 1GB, to change it edit the "IMAGESIZE" variable in the Makefile.

After this we can insert the module and mount the filesystem with the file on a virtual block device with:

```
insmod ransomfs.ko
mount -o loop -t ransomfs test.img <directory>
```

The File System will be mounted with the defult password ```1234```, to umount we need to call the "umount_ctl" syscall with this password, so run:

```
user/unlock <directory> 1234
umount <directory>
```

### The easy way

Go in the tests folder, and to mount run:

```
sudo su
. ./utils.sh
standard_setup
```

This will create a temporary image in the /tmp folder and mount the FS to /tmp/mnt, to umount:

```
sudo su
. ./utils.sh
standard_cleanup
```

Care, these functions will work only while the PWD is the tests folder.

## Tests

Currently 2 tests have been implemented:
- Big File Test, creates and copies a "big" file to the FS, used to make sure that file can span multiple groups without problems;
- Extent resize, creates to files and write to them in way to get them to have a non-consecutive block allocation, used to verify that an extent tree will get correctly resized if it doesn't fit anymore in his block;

To run all tests make sure you in the test folder and run:

```
./run_all_tests <DEBUG> # add DEBUG to see the full output
```

## Current Progress

| State | Task | Notes |
|:-:|:-:|:-:|
| :heavy_check_mark: | FileSystem registration | |
| :heavy_check_mark: | Mounting and Unmounting | |
| :heavy_check_mark: | Directory Read | |
| :heavy_check_mark: | File/Directory Creation | |
| :heavy_check_mark: | File Write | |
| :heavy_check_mark: | File Read | |
| :heavy_check_mark: | Write Protection | |
| :heavy_check_mark: | Umount Protection | |
| :heavy_check_mark: | Block Device Protection | |

## TODOs

| State | Task | Difficulty |
|:-:|:-|:-:|
|:x:| Create some sort of callback mechanism for the traversing of the extent tree | 5/5 |
|:heavy_check_mark:| Implement concurrency management on the cached gdt | 1/5 |
|:heavy_check_mark:| I ignored a lot of concurrency problems while programming, need to fix this | 3/5 |
|:heavy_check_mark:| The search for blocks in the allocation needs to be improved in term of closeness to the other blocks | 4/5 |
