#!/bin/bash

if [ -z "$APPS" ]; then
        echo "APPS environment variable is undefined."
        echo "Did you setvars? goto Base directory and $ source ./scripts/setvars.sh"
        exit 1
fi

##This script would run strided MADBench and collect its results
source $RUN_SCRIPTS/generic_funcs.sh
APPPREFIX="/usr/bin/time -v"

ulimit -n 1000000

DBHOME=$PWD
THREAD=16
VALUE_SIZE=4096
SYNC=0
KEYSIZE=1000
WRITE_BUFF_SIZE=67108864
NUM=20000000
DBDIR=$DBHOME/DATA
base=$PWD

slack_MB=1024

MEM_BUDGET_PER='0.7'

declare -a mem_bud=("1" "0.7" "0.5" "0.2")

declare -a num_arr=("20000000")
declare -a config_arr=("Vanilla" "OSonly" "Cross_Info_sync" "CIPB")

WORKLOAD="multireadrandom"
#WORKLOAD="readrandom"
#WORKLOAD="readreverse"
#READARGS="--benchmarks=$WORKLOAD --use_existing_db=1 --mmap_read=0 --threads=$THREAD"
WRITEARGS="--benchmarks=fillseq --use_existing_db=0 --threads=1"

ORI_PARAMS="--db=$DBDIR --wal_dir=$DBDIR/WAL_LOG --sync=$SYNC --write_buffer_size=$WRITE_BUFF_SIZE"
ORI_READARGS="--use_existing_db=1 --mmap_read=0"
#READARGS="--benchmarks=$WORKLOAD --use_existing_db=1 --mmap_read=0 --threads=$THREAD --advise_random_on_open=false --readahead_size=2097152 --compaction_readahead_size=2097152 --log_readahead_size=2097152"
APPPREFIX="/usr/bin/time -v"


umount_2ext4ramdisk

CLEAR_PWD()
{
        cd $DBDIR
        rm -rf *.sst CURRENT IDENTITY LOCK MANIFEST-* OPTIONS-* WAL_LOG/
        cd ..
}


CLEAN_AND_WRITE()
{
        printf "in ${FUNCNAME[0]}\n"

        export LD_PRELOAD=""
        CLEAR_PWD
        $DBHOME/db_bench $PARAMS $WRITEARGS
        FlushDisk

        ##Condition the DB to get Stable results
        $DBHOME/db_bench $PARAMS $ORI_READARGS --benchmarks=readseq --threads=16
        FlushDisk
        $DBHOME/db_bench $PARAMS $ORI_READARGS --benchmarks=readseq --threads=16
        FlushDisk
}

for NUM in "${num_arr[@]}"
do
        PARAMS="--db=$DBDIR --value_size=$VALUE_SIZE --wal_dir=$DBDIR/WAL_LOG --sync=$SYNC --key_size=$KEYSIZE --write_buffer_size=$WRITE_BUFF_SIZE --num=$NUM  --seed=1576170874"

        #CLEAN_AND_WRITE
        FlushDisk

        printf "\nRUNNING Memlimit.................\n"
        export LD_PRELOAD=/usr/lib/lib_memusage.so
        $DBHOME/db_bench $PARAMS $ORI_READARGS --benchmarks=readseq --threads=16 &> out_memusage
        export LD_PRELOAD=""
        FlushDisk

        for MEM_BUDGET_PER in "${mem_bud[@]}"
        do
                total_anon_MB=`cat out_memusage | grep "total_anon_used" | awk '{print $2}'`
                total_cache_MB=`cat out_memusage | grep "total_anon_used" | awk '{print $5}'`

                echo "total_anon_MB = $total_anon_MB"
                echo "total_cache_MB = $total_cache_MB"

                free -h
                SETUPEXTRAM_2 `echo "scale=0; (($total_anon_MB + ($total_cache_MB*$MEM_BUDGET_PER))+$slack_MB)/1" | bc --mathlib`
                free -h

                for CONFIG in "${config_arr[@]}"
                do
                        printf "\nrunning $CONFIG MEM=$MEM_BUDGET_PER WORKLOAD=$WORKLOAD NUM=$NUM THREADS=$THREAD....\n"

                        READARGS="$ORI_READARGS --benchmarks=$WORKLOAD --threads=$THREAD"

                        RESULTFILE="mb_${WORKLOAD}_${CONFIG}_${THREAD}_${NUM}_${MEM_BUDGET_PER}"

                        export LD_PRELOAD=/usr/lib/lib_$CONFIG.so
                        $APPPREFIX $DBHOME/db_bench $PARAMS $READARGS &> $RESULTFILE
                        export LD_PRELOAD=""
                        sudo dmesg -c &>> $RESULTFILE
                        FlushDisk
                done
                umount_2ext4ramdisk
        done
done
