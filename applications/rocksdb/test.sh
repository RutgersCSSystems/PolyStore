#!/bin/bash

if [ -z "$APPS" ]; then
        echo "APPS environment variable is undefined."
        echo "Did you setvars? goto Base directory and $ source ./scripts/setvars.sh"
        exit 1
fi

##This script would run strided MADBench and collect its results
source $RUN_SCRIPTS/generic_funcs.sh

ulimit -n 1000000

DBHOME=$PWD
THREAD=4
VALUE_SIZE=4096
SYNC=0
KEYSIZE=1000
WRITE_BUFF_SIZE=67108864
NUM=5000000
DBDIR=$DBHOME/DATA

#Require for large database
ulimit -n 1000000 

#DEV=/dev/nvme0n1p1

#BLOCK_SZ=512 #Bytes
#RA_SIZE=128 #KB

#NR_RA_BLOCKS=`echo "($RA_SIZE*1024)/$BLOCK_SZ" | bc`

KB=1024
MB=`echo "1024*$KB" | bc`
GB=`echo "1024*$MB" | bc`

#sudo blockdev --setra $NR_RA_BLOCKS $DEV

#WORKLOAD="readseq"
WORKLOAD="readrandom"
#WORKLOAD="readreverse"
WRITEARGS="--benchmarks=fillseq --use_existing_db=0 --threads=1"
READARGS="--benchmarks=$WORKLOAD --use_existing_db=1 --mmap_read=0 --threads=$THREAD"
#READARGS="--benchmarks=$WORKLOAD --use_existing_db=1 --mmap_read=0 --threads=$THREAD --advise_random_on_open=false"
#READARGS="--benchmarks=$WORKLOAD --use_existing_db=1 --mmap_read=0 --threads=$THREAD --advise_random_on_open=false --readahead_size=2097152 --compaction_readahead_size=2097152 --log_readahead_size=2097152"
APPPREFIX="/usr/bin/time -v"

PARAMS="--db=$DBDIR --value_size=$VALUE_SIZE --wal_dir=$DBDIR/WAL_LOG --sync=$SYNC --key_size=$KEYSIZE --write_buffer_size=$WRITE_BUFF_SIZE --num=$NUM"

FlushDisk()
{
    sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
    sudo sh -c "sync"
    sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
    sudo sh -c "sync"
    #sudo dmesg --clear
}


ENABLE_LOCK_STATS()
{
	sudo sh -c "echo 0 > /proc/lock_stat"
	sudo sh -c "echo 1 > /proc/sys/kernel/lock_stat"
}

DISABLE_LOCK_STATS()
{
	sudo sh -c "echo 0 > /proc/sys/kernel/lock_stat"
}

CLEAR_PWD()
{
    cd $DBDIR
    rm -rf *.sst CURRENT IDENTITY LOCK MANIFEST-* OPTIONS-* WAL_LOG/
    cd ..
}

#DISABLE_LOCK_STATS

#Run write workload twice
#CLEAR_PWD
#$DBHOME/db_bench $PARAMS $WRITEARGS
#FlushDisk
#exit

#LOCKDAT=$PWD/lockdat
#mkdir $LOCKDAT
sudo dmesg --clear


<< 'MULTILINE-COMMENT'
echo "RUNNING Vanilla................."
FlushDisk
clear_os_stats
#ENABLE_LOCK_STATS
export LD_PRELOAD=""
$DBHOME/db_bench $PARAMS $READARGS 
export LD_PRELOAD=""
#DISABLE_LOCK_STATS
FlushDisk
dmesg
sudo dmesg --clear
#sudo cat /proc/lock_stat

echo "RUNNING OSONLY................"
FlushDisk
clear_os_stats
SETPRELOAD "OSONLY"
$DBHOME/db_bench $PARAMS $READARGS 
export LD_PRELOAD=""
FlushDisk
dmesg
sudo dmesg --clear

echo "RUNNING Cross_Info................"
FlushDisk
clear_os_stats
export LD_PRELOAD="/usr/lib/lib_Cross_Info.so"
$DBHOME/db_bench $PARAMS $READARGS 
export LD_PRELOAD=""
FlushDisk
dmesg
sudo dmesg --clear

MULTILINE-COMMENT

echo "RUNNING Cross Info Predict................"
FlushDisk
clear_os_stats
#SETPRELOAD "CPNI"
export LD_PRELOAD="/usr/lib/lib_CIP.so"
$DBHOME/db_bench $PARAMS $READARGS
export LD_PRELOAD=""
FlushDisk
dmesg
sudo dmesg --clear

#/users/shaleen/ssd/ltrace/ltrace -w 5 -rfSC -l /usr/lib/libnopred.so $DBHOME/db_bench $PARAMS $READARGS
