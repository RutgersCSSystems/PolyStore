#!/bin/bash

# https://github.com/josehu07/open-cas-linux-mf

set -x

# Step 1: Terminate MFC cache
sudo casadm -Q -i 1 -c pt
sudo casadm -N

# Step 2: umount CAS device
sudo umount /mnt/orthus

# Step 3: Terminate OpenCAS
sudo casadm -T -i 1

set +x
