#!/bin/bash

# https://github.com/josehu07/open-cas-linux-mf

set -x

# Step 1: format the target device
sudo mkfs.ext4 /dev/nvme1n1p1

# Step 2: setup the cache and add a core
sudo wipefs -a /dev/pmem0
sudo casadm -S --force -d /dev/pmem0
sudo casadm -A -d /dev/nvme1n1p1 -i 1
sudo casadm -L

# Step 3: start the cache
sudo casadm -M -i 1 -j 1
sudo casadm -Q -i 1 -c mfwb
sudo casadm -L

# Step 4: mount CAS device
if [ ! -d /mnt/orthus ]; then
	sudo mkdir /mnt/orthus
fi
sudo mount /dev/cas1-1 /mnt/orthus
sudo chown -R $USER /mnt/orthus

set +x
