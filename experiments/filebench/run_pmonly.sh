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
	rm -rf /mnt/fast/*
}

ResetFile

echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
$FILEBENCH_PATH/filebench -f $FILEBENCH_PATH/workloads_pmonly/fileserver_32.f
#$FILEBENCH_PATH/filebench -f $FILEBENCH_PATH/workloads_pmonly/varmail_16.f
echo 2 | sudo tee /proc/sys/kernel/randomize_va_space



