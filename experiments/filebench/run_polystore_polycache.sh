#!/bin/bash

cd ../../
source scripts/setvars.sh
cd experiments/filebench

# Output result directory
result_dir=$RESULTS_PATH/filebench/polystore-test

# Setup parameters
declare -a threadarr=("1" "2" "4" "8" "16")
declare -a workloadarr=("fileserver" "webserver")

sudo bash -c "ulimit -u 10000000"
sudo sysctl -w fs.file-max=10000000

FlushDisk() {
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
        sudo sh -c "sync"
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

ResetFile() {
	rm -rf /mnt/fast/.persist/*
	rm -rf /mnt/fast/*
	rm -rf /mnt/slow/*
}

LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$POLYLIB_PATH/libs/syscall-intercept/build/:$POLYLIB_PATH/libs/interval-tree/
export LD_LIBRARY_PATH
#export INTERCEPT_LOG="./intercept_log"
#export INTERCEPT_LOG_TRUNC=0

ResetFile

# Load PolyOS module
$BASE/scripts/polyos_install.sh

export POLYSTORE_SCHED_SPLIT_POINT=8
export POLYSTORE_POLYCACHE_POLICY=2

echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
#LD_PRELOAD=$POLYLIB_PATH/build/libpolystore_cache_shm.so perf record -g -o perf.data $FILEBENCH_PATH/filebench -f $FILEBENCH_PATH/workloads_polystore/fileserver_16.f
numactl --cpunodebind=0 env LD_PRELOAD=$POLYLIB_PATH/build/libpolystore_cache_shm.so $FILEBENCH_PATH/filebench -f $FILEBENCH_PATH/workloads_polystore/fileserver_16.f
#numactl --cpunodebind=0 env LD_PRELOAD=$POLYLIB_PATH/build/libpolystore_cache_shm.so $FILEBENCH_PATH/filebench -f $FILEBENCH_PATH/workloads_polystore/varmail_16.f
echo 2 | sudo tee /proc/sys/kernel/randomize_va_space

# Uninstall PolyOS module
$BASE/scripts/polyos_uninstall.sh
