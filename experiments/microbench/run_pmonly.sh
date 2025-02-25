#!/bin/bash

cd ../../
source scripts/setvars.sh
cd experiments/microbench

# Output result directory
result_dir=$RESULTS_PATH/microbench/polystore-test

# Setup parameters
declare -a workloadarr=("seqwrite" "randread" "randwrite" "seqread")
declare -a workloadidarr=("2" "3" "4" "1")

FlushDisk() {
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
        sudo sh -c "sync"
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

ResetFiles() {
	for t in {0..31}
	do
        	if [ ! -f $FAST_DIR/thread_$t ]; then
               		touch $FAST_DIR/thread_$t
        	else
                	truncate -s 0 $FAST_DIR/thread_$t
        	fi
	done
}

# Generate Testing files in FAST and SLOW device
if [ ! -d $FAST_DIR ]; then
        mkdir $FAST_DIR
fi

FlushDisk

# Run benchmark
for j in {0..3}
do
	workload=${workloadarr[j]}
	workloadid=${workloadidarr[j]}
	thread=32
	if [ $j -eq 0 ]
	then
		ResetFiles
		FlushDisk
	fi
	echo "start $workload $thread"

	echo "WORKLOADID"$workloadid

	numactl --cpunodebind=0 $MICROBENCH_PATH/hetero_io_bench -r $thread -j $workloadid -s 4096 -t 2 -y 0 -x 0 -z 1g -f 1 -d 1

	echo "end $workload $thread"
	FlushDisk
	sleep 2
done
