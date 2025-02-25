#!/bin/bash                                                                                                                                                                  

USER=$(whoami)
MOUNTPOINT=/mnt/ram
DEFAULT_SIZE=4
SIZE=${1:-$DEFAULT_SIZE}

echo $USER

if [ $(mount | grep -c $MOUNTPOINT) != 1 ]; then
	if [ ! -d $MOUNTPOINT ]; then
		sudo mkdir $MOUNTPOINT
	fi
	sudo mount -t tmpfs -o size="$SIZE"g tmpfs $MOUNTPOINT
	sudo chown -R $USER $MOUNTPOINT

	echo "$MOUNTPOINT is now mounted with size $SIZE g"
else
	echo "$MOUNTPOINT already mounted with size $SIZE g"
fi

# Populating file
touch /mnt/ram/anchor
dd if=/dev/zero of=/mnt/ram/anchor bs=1G count="$SIZE"

sudo chown -R $USER $MOUNTPOINT
