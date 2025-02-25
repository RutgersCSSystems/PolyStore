#!/bin/bash 

set -x 

cd ../../
source scripts/setvars.sh
cd experiments/rocksdb

# Output result directory
result_dir=$RESULTS_PATH/polystore

# Setup Parameters
declare -a benchmarkarr=("fillrandom" "readrandom" "readwhilewriting")
declare -a parameterarr=("" "--use_existing_db=1" "--use_existing_db=1")
VALUES=500000
VALUESIZE=4096
BGTHREADS=16
DBDATA_PATH=$POLYSTORE_DIR/db

#export BENCHMARK_HOOK=$PWD/clear-cache.sh

FlushDisk() {
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
        sudo sh -c "sync"
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

ResetFiles() {
	rm -rf $FAST_DIR/.persist/
        rm -rf $FAST_DIR/db
        rm -rf $SLOW_DIR/db
}

# Run benchmark
THREADS=32

LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$POLYLIB_PATH/libs/syscall-intercept/build/:$POLYLIB_PATH/libs/interval-tree/
export LD_LIBRARY_PATH
#export INTERCEPT_LOG="./intercept_log"
#export INTERCEPT_LOG_TRUNC=0

# Load PolyOS module
$BASE/scripts/polyos_install.sh

export POLYSTORE_SCHED_SPLIT_POINT=8

echo "start configuration of $THREADS threads"

for i in {0..1}
do
	if [ $i -eq 0 ]; then
		ResetFiles
	fi

	benchmark=${benchmarkarr[i]}
	param=${parameterarr[i]}

	numactl --cpunodebind=0 env LD_PRELOAD=$POLYLIB_PATH/build/libpolystore.so $ROCKSDB_PATH/db_bench --db=$DBDATA_PATH --num_levels=6 --key_size=20 --prefix_size=20 --bloom_bits=10 --bloom_locality=1 --max_background_compactions=$BGTHREADS --max_background_flushes=$BGTHREADS --benchmarks=$benchmark --num=$VALUES --compression_type=none --value_size=$VALUESIZE --threads=$THREADS $param

	sleep 2
done

echo "end configuration of $THREADS threads"

# Uninstall PolyOS module
$BASE/scripts/polyos_uninstall.sh

set +x
