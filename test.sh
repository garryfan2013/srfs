#!/bin/bash

MODULE_NAME=srfs
MOUNT_POINT=/mnt/srfs
MODULE_SRC=/root/srfs

# create mount point
if [[ ! -d "$MOUNT_POINT" ]]; then
	mkdir -p "$MOUNT_POINT" || \
		{ echo "mkdir -p $MOUNT_POINT failed"; exit 1; }
fi

# recompile the module
cd $MODULE_SRC || { echo "cd $MODULE_SRC failed"; exit 1; }
make clean || exit 2
make || exit 2

# umount the fs
mount | grep $MOUNT_POINT > /dev/null 2>&1
if [ $? = 0 ]; then
    umount $MOUNT_POINT > /dev/null 2>&1 || \
        { echo "umount $MOUNT_POINT failed";exit 1; }
fi

# uninstall the kernel module
lsmod | grep $MODULE_NAME 2>&1 > /dev/null
if [ $? = 0 ]; then
    rmmod $MODULE_NAME || \
        { echo "rmmod $MODULE_NAME failed";exit 1; }
fi

# reinstall the kernel module
insmod $MODULE_NAME.ko || exit 1
mount -t $MODULE_NAME "fan" $MOUNT_POINT || exit 1

# ftruncate test block
TEST_FILE=$MOUNT_POINT/file1
touch $TEST_FILE || { echo "touch file $TEST_FILE failed"; exit 1; }
echo "1234567890" >> $TEST_FILE || { echo "write file $TEST_FILE failed"; exit 1; }
