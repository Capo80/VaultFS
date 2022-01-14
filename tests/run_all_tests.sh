#!/bin/bash
. ./utils.sh

print_delimiter

for test in *_test.sh;
do

    center $test
    print_delimiter

    chmod +x $test

    if [[ $1 == "DEBUG" ]]
    then 
        ./$test
    else
        ./$test 2> /dev/null | grep "_test"
    fi

    print_delimiter

done;