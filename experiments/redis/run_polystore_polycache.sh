#!/bin/bash
#set -x

cd ../../
source scripts/setvars.sh
cd experiments/redis

REDISCONF=$REDIS_PATH/redis-conf
REDISDIR=$REDIS_PATH/src
DBDIR=$MOUNT_POLYSTORE
#PWD=$DBDIR

let MAXINST=1
let STARTPORT=6378
let KEYS=100000

PARAFS=$BASE
DIR=""
result_dir=$RESULTS_PATH/redis
mkdir -p $result_dir

# Create output directories
if [ ! -d "$result_dir" ]; then
	mkdir -p $result_dir
fi

CLEAN() {
	for (( b=1; b<=$MAXINST; b++ ))
	do
		rm -rf $DBDIR/*.rdb
		rm -rf $DBDIR/*.aof
		sudo pkill "redis-server$b"
		echo "KILLING redis-server$b"
	done
}

PREPARE() {
	for (( inst=1; inst<=$MAXINST; inst++ ))
	do
		cp $REDISDIR/redis-server $REDISDIR/redis-server$inst
	done
}

FlushDisk() {
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
        sudo sh -c "sync"
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

RUN_SERVER(){
	echo "MAXINST... $MAXINST"
	let port=$STARTPORT
	for (( r=1; r<=$MAXINST; r++))
	do
		LD_PRELOAD=$POLYLIB_PATH/build/libpolystore_cache.so $REDISDIR/redis-server$r $REDISCONF/redis-$port".conf" &
		let port=$port+1
	done
}

RUN_CLIENT(){
	let port=$STARTPORT
	$REDISDIR/redis-benchmark -t set -p $port -q -n $KEYS -d $1
	sleep 5
}

TERMINATE() {
	for (( c=1; c<=$MAXINST; c++))
	do
		sudo pkill redis-server$i
		sudo pkill redis-server$i
	done
	sudo pkill redis-benchmark
}

LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$POLYLIB_PATH/libs/syscall-intercept/build/:$POLYLIB_PATH/libs/interval-tree/
export LD_LIBRARY_PATH
#export INTERCEPT_LOG="./intercept_log"
#export INTERCEPT_LOG_TRUNC=0

CLEAN
PREPARE

for i in 1
do
	echo "Going to sleep waiting for redis servers to terminate gracefully"
	sleep 5

	let MAXINST=$i
	DIR=$result_dir/$MAXINST"-inst"
	mkdir -p $DIR

	CLEAN
	FlushDisk

	RUN_SERVER
	sleep 8
	RUN_CLIENT 1024

	TERMINATE
done

CLEAN
echo "Finished all the test, going to sleep 16 sec waiting for redis servers to terminate gracefully"
sleep 10
