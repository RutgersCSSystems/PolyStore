#!/bin/bash

cd ../
source scripts/setvars.sh
cd tools

FlushDisk() {
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
        sudo sh -c "sync"
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

FlushDisk

export HETERO_DIR=$POLYSTORE_DIR

LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$POLYLIB_PATH/libs/syscall-intercept/build/:$POLYLIB_PATH/libs/interval-tree/
export LD_LIBRARY_PATH
#export INTERCEPT_LOG="./intercept_log"
#export INTERCEPT_LOG_TRUNC=0

# Run benchmark
LD_PRELOAD=$POLYLIB_PATH/build/libpolystore_filecopy.so ./filecopy /users/ingerido/ssd/Dataset/com-friendster.ungraph.txt /mnt/polystore/graph.txt
