#!/bin/bash
. ./utils.sh

ROOTUID="0"

test_file_name=$(echo "$0" | sed "s/.*\///")
test_name=$(echo "$test_file_name" | sed 's/\.[^.]*$//')

if [ "$(id -u)" -ne "$ROOTUID" ] ; 
then
    echo $Red"[$test_name] This script must be executed with root privileges."$Color_Off;
    exit -1
fi

print_update "[$test_name] Starting test...";

mount_path="/tmp/mnt"

insert_module ../src
mkdir -p $mount_path

for FILE_SIZE in 125M 250M 500M 1G 2G
do

    #set up module
    #format_image ../src/mkfs.vaultfs /dev/sdb1 1234
    #mount -t vaultfs /dev/sdb1 $mount_path
    mkfs.ext4 /dev/sdb1
    mount -t ext4 /dev/sdb1 $mount_path

    head -c $FILE_SIZE < /dev/urandom > /tmp/testfile

    print_update "[$test_name] Copying file of size $FILE_SIZE to FS..."
    time cp /tmp/testfile $mount_path_$FILE_SIZE

    rm /tmp/testfile

    #clean up
    unlock_umount ../src/user/unlock
    umount $mount_path

done

remove_module ../src
rmdir $mount_path