#!/bin/bash 

USER=$(whoami)
DEFAULT_DEVICE=/dev/pmem0
DEFAULT_MOUNTPOINT=$FAST_DIR
DEVICE=${1:-$DEFAULT_DEVICE}
MOUNTPOINT=${2:-$DEFAULT_MOUNTPOINT}

echo $USER

if [ $(mount | grep -c $MOUNTPOINT) != 1 ]; then
	sudo umount $DEVICE
	if [ ! -d $MOUNTPOINT ]; then
		sudo mkdir $MOUNTPOINT
	fi
	sudo mkfs -t ext4 -b 4096 $DEVICE
	sudo mount -o dax $DEVICE $MOUNTPOINT 

	sudo chown -R $USER $MOUNTPOINT

	echo "$MOUNTPOINT is now mounted"
else
	echo "$MOUNTPOINT already mounted"
fi

sudo chown -R $USER $MOUNTPOINT
