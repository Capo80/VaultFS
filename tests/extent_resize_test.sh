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

print_update "[$test_name] Compiling test...";
gcc extent_resize.c -o extent_resize

print_update "[$test_name] Running test...";
./extent_resize $mount_path
result=$?

if [ $result == 0 ]
then 
    print_update "[$test_name] Creating check files...";
    printf %67500s | tr " " "A" > /tmp/file1
    printf %67500s | tr " " "B" > /tmp/file2

    print_update "[$test_name] Checking diff...";
    diff /tmp/file1 $mount_path/file1
    result1=$?
    diff /tmp/file2 $mount_path/file2
    result2=$?

    print_update "[$test_name] Cleaning up check files...";
    rm /tmp/file1
    rm /tmp/file2

fi

print_update "[$test_name] Cleaning up test executable...";
rm extent_resize


#clean up
print_update "[$test_name] Cleaning up File System..."
standard_cleanup

#print test result
if [[ $result1 -eq 0 && $result2 -eq 0 && $result -eq 0 ]]
then
    echo $Green"[$test_name] [SUCCESS] File has been copied correctly"$Color_Off;
else
    echo $Red"[$test_name] [FAILED] File has not been copied correctly"$Color_Off;
fi
