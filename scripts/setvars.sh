#!/bin/bash

set_env_vars () {
	set -x

	# Some system info

	export SCRIPTS_PATH=$PWD/scripts

	#Pass the release name
	export BASE=$PWD
	export OS_RELEASE_NAME=$(sed -n 's/^\UBUNTU_CODENAME=//p' < /etc/os-release)

	# Applications 
	export APP_PATH=$PWD/applications

	# Benchmarks 
	export BENCHMARK_PATH=$PWD/benchmarks

	# KERNEL
	export KERNEL=$PWD/kernel
	export KERNEL_SRC=$PWD/kernel/linux-nova

	# CPU parallelism
	export PARA=-j16

	# Storage Mountpoint
	export FAST_DIR=/mnt/fast
	export SLOW_DIR=/mnt/slow
	export ORTHUS_DIR=/mnt/orthus
	export SPFS_DIR=/mnt/spfs
	export BCACHE_DIR=/mnt/bcache
	export POLYSTORE_DIR=/mnt/polystore

        # PolyStore Library Path
	export POLYLIB_PATH=$PWD/polylib

        # PolyOS Path
	export POLYOS_PATH=$POLYLIB_PATH/src/polyos

	# Microbench
	export MICROBENCH_PATH=$BENCHMARK_PATH/microbench

	# Filebench
	export FILEBENCH_PATH=$BENCHMARK_PATH/filebench

	# RocksDB variables
	export ROCKSDB_PATH=$APP_PATH/rocksdb

	# Redis variables
	export REDIS_PATH=$APP_PATH/redis

	# GraphWalker variables
	export GRAPHWALKER_PATH=$APP_PATH/graphwalker
	export GRAPHDATASET_PATH=/mnt/dataset

	# VTUNE variables
	export VTUNE_PATH=/opt/intel/vtune_amplifier_2019.4.0.597835 

	# Results variables
	export RESULTS_PATH=$PWD/resultgen/results/$USER

	set +x
}

install_libs () {
	set -x
	sudo apt-get update -y
	sudo apt-get install -y pandoc numactl pkg-config libcapstone-dev cmake 
	sudo apt-get install -y libssl-dev libelf-dev libncurses-dev libdpkg-dev flex bison
	set +x
}

# Check if this script is being sourced
if [ $_ != $0 ]; then
	: #echo "This is sourced"
else
	echo "This is a subshell, you need to run this as source"
	exit 1
fi

# Check if executing in correct position
stat $PWD/scripts/setvars.sh &> /dev/null

if [ $? -ne 0 ]; then
	echo "Not executing shetvars from correct source"
	return
fi

set_env_vars
#install_libs

set +x
