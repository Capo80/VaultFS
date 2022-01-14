#!/bin/bash

Red="$(tput setaf 1)" 
Green="$(tput setaf 2)" 
Yellow="$(tput setaf 3)" 
Cyan="$(tput setaf 6)"
Blue="$(tput setaf 4)"
Magenta="$(tput setaf 5)"
Color_Off="$(tput sgr0)"  # Text Reset

# $1: image path - /tmp/test.img if empty
# $2: image size (expresseed in Megabytes) - 1GB if empty
create_image () {

    if [ -z "$1" ]
    then
        image_path="/tmp/test.img"
    else
        image_path=$1
    fi

    if [ $# -lt 1 ]
    then
        image_size="1000"
    else
        image_size=$2
    fi

    echo "Creting Image of size" $image_size "Mb in" $image_path"..."

	dd if=/dev/zero of=$image_path bs=1M count=$image_size

}

# $1: image path - /tmp/test.img if empty
delete_image() {

    if [ -z "$1" ]
    then
        image_path="/tmp/test.img"
    else
        image_path=$1
    fi

    echo "Deleting" $image_path"..."

    rm $image_path
}

# $1: path of the mkfs executable - cannot be empty
# $2: image path - /tmp/test.img if empty
# $3: FS password - 1234 if empty
format_image() {
    
    if [ -z "$2" ]
    then
        image_path="/tmp/test.img"
    else
        image_path=$2
    fi

    if [ -z "$3" ]
    then
        image_passwd="1234"
    else
        image_passwd=$3
    fi

    echo "Formattimg" $image_path "for RansomFS..."

    $1 $image_path $image_passwd

}

# $1 path of folder containing Makefile - cannot be empty
# needs to be run as root
insert_module() {

    echo "Compiling module..."

    cd $1
    make module
    make mkfs.ransomfs
    echo "Inserting module..."
    insmod ransomfs.ko
    cd -

}

# $1 path of the image - /tmp/test.img if empty
# $2 path of the mount - /tmp/mnt if empty
# needs to be run as root
mount_fs() {

    if [ -z "$1" ]
    then
        image_path="/tmp/test.img"
    else
        image_path=$1
    fi

    if [ -z "$2" ]
    then
        mount_path="/tmp/mnt"
    else
        mount_path=$2
    fi

    echo "Mounting FS in" $mount_path"...";

    mkdir -p $mount_path
    mount -o loop -t ransomfs $image_path $mount_path/

}

# $1 path of unlock executable - cannot be empty
# $2 path of the mount - /tmp/mnt if empty
# $3 password for the umount - 1234 if empty
unlock_umount() {

    if [ -z "$2" ]
    then
        mount_path="/tmp/mnt"
    else
        mount_path=$2
    fi

    if [ -z "$3" ]
    then
        mount_passwd="1234"
    else
        mount_passwd=$3
    fi

    $1 $mount_path $mount_passwd

}

# $1 path of the mount - /tmp/mnt if empty
umount_fs() {

    if [ -z "$1" ]
    then
        mount_path="/tmp/mnt"
    else
        mount_path=$1
    fi

    echo "Unmounting FS..."

    umount $mount_path
    rmdir $mount_path

}

# $1 path of folder containing Makefile - cannot be empty
# needs to be run as sudo
remove_module() {

    echo "Removing module module..."

    cd $1
    rmmod ransomfs
    make clean
    cd -

}

standard_setup() {

    insert_module ../src
    create_image
    format_image ../src/mkfs.ransomfs
    mount_fs

}

standard_cleanup() {

    unlock_umount ../src/user/unlock
    umount_fs
    delete_image
    remove_module ../src

}

print_delimiter() {

    echo -n $Cyan

    x=0
    terminal_size=$(stty size | cut -d" " -f2)
    while [ $x -lt $terminal_size ]; do echo -n '='; let x=$x+1; done; echo;

    echo -n $Color_Off
}

print_update() {

    echo -n $White

    echo $1

    echo -n $Color_Off

}

center() {
    termwidth="$(tput cols)"
    padding="$(printf '%0.1s' ={1..500})"

    echo -n $Cyan

    printf '%*.*s %s %*.*s\n' 0 "$(((termwidth-2-${#1})/2))" "$padding" "$1" 0 "$(((termwidth-1-${#1})/2))" "$padding"

    echo -n $Color_Off

}

# insert_module ../src
# create_image
# format_image ../src/mkfs.ransomfs
# mount_fs
# unlock_umount ../src/user/unlock
# umount_fs 
# delete_image
# remove_module ../src