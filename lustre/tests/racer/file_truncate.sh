#!/bin/bash

DIR=$1
MAX=$2

while /bin/true ; do
    file=$((RANDOM % MAX))
    dd if=/dev/zero of=$DIR/$file count=1 bs=4096 > /dev/null 2>&1
    /usr/bin/truncate $DIR/$file --size 64 > /dev/null 2>&1
done
