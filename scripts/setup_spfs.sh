#!/bin/bash

# https://github.com/DICL/spfs

set -x

# Step 1: format the target device
sudo mkfs.ext4 /dev/nvme1n1p1

# Step 2: Wipe Pmem
sudo wipefs -a /dev/pmem0

# Step 3: Mount block device file system
if [ ! -d /mnt/spfs ]; then
	sudo mkdir /mnt/spfs
fi
sudo mount -t ext4 /dev/nvme1n1p1 /mnt/spfs

# Step 4: Mount SPFS on the top of the block device file system
sudo mount -t spfs -o pmem=/dev/pmem0,format,consistency=meta /mnt/spfs /mnt/spfs

# Make sure we have permissions to mount point
sudo chown -R $USER /mnt/spfs

# Double check mount
mount | grep "/mnt/spfs"

set +x
