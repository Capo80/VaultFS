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

print_update "[$test_name] Setting up File System...";

#set up module
standard_setup

print_update "[$test_name] Creating file of size 500Mb..."
head -c 500M < /dev/urandom > /tmp/testfile

print_update "[$test_name] Copying to FS..."
cp /tmp/testfile $mount_path

print_update "[$test_name] Checking diff..."
diff /tmp/testfile $mount_path/testfile
result=$?

print_update "[$test_name] Cleaning up file..."
rm /tmp/testfile

#clean up
print_update "[$test_name] Cleaning up File System..."
standard_cleanup


#print test result
if [ $result -eq 0 ]
then
    echo $Green"[$test_name] [SUCCESS] File has been copied correctly"$Color_Off;
else
    echo $Red"[$test_name] [FAILED] File has not been copied correctly"$Color_Off;
fi
