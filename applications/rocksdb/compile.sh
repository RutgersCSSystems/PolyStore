#!/bin/bash

# Requires the follow enviroment vartiables to be set:
#  1.APPS

sudo apt update
sudo apt-get install -y libgflags2.2 libgflags-dev libsnappy-dev zlib1g-dev libbz2-dev liblz4-dev libzstd-dev

#rocksdb_clean

echo "compiling rocksdb"
make clean
DEBUG_LEVEL=0 CFLAGS=-Wno-error make db_bench -j$(nproc)
