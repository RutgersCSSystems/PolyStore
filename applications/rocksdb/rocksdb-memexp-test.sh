#!/bin/bash
set -x
DBHOME=$PWD
THREAD=16
VALUE_SIZE=4096
SYNC=0
KEYSIZE=1000
WRITE_BUFF_SIZE=67108864
DBDIR=$DBHOME/DATA
#DBDIR=/mnt/remote/DATA


if [ -z "$APPS" ]; then
        echo "APPS environment variable is undefined."
        echo "Did you setvars? goto Base directory and $ source ./scripts/setvars.sh"
        exit 1
fi

##This script would run strided MADBench and collect its results
source $RUN_SCRIPTS/generic_funcs.sh


#WORKLOAD="readseq"
#WORKLOAD="readreverse"

WORKLOAD="readrandom"
WRITEARGS="--benchmarks=fillseq --use_existing_db=0 --threads=1"
READARGS="--benchmarks=$WORKLOAD --use_existing_db=1 --mmap_read=0 --threads=$THREAD"
#READARGS="--benchmarks=$WORKLOAD --use_existing_db=1 --mmap_read=0 --threads=$THREAD --advise_random_on_open=false --readahead_size=2097152 --compaction_readahead_size=2097152 --log_readahead_size=2097152"
APPPREFIX="/usr/bin/time -v"

APP=db_bench
APPOUTPUTNAME="ROCKSDB-membudget"

RESULTS="RESULTS"/$WORKLOAD
RESULTFILE=""

mkdir -p $RESULTS

declare -a num_arr=("40000000")
NUM=40000000

#declare -a workload_arr=("readrandom" "readseq" "readreverse" "compact" "overwrite" "readwhilewriting" "readwhilescanning")
#declare -a thread_arr=("4" "8" "16" "32")
#declare -a config_arr=("Vanilla" "Cross_Naive" "CPBI" "CNI" "CPBV" "CPNV" "CPNI")

declare -a thread_arr=("16")

#declare -a workload_arr=("readseq" "readrandom" "readwhilescanning")
declare -a workload_arr=("readrandom")
declare -a config_arr=("OSonly" "Cross_Info_Eviction")
#declare -a config_arr=("Cross_Naive" "CNI" "CPNI")


#Require for large database
ulimit -n 1000000 


FlushDisk()
{
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
        #sudo dmesg --clear
        sleep 5
}

CLEAR_DATA()
{
        sudo killall $APP
        sudo killall $APP
        sleep 3
        sudo killall $APP
        rm -rf $DBDIR/*
        rm -rf *.sst CURRENT IDENTITY LOCK MANIFEST-* OPTIONS-* WAL_LOG/
}



COMPILE_AND_WRITE()
{
        export LD_PRELOAD=""
        PARAMS="--db=$DBDIR --value_size=$VALUE_SIZE --wal_dir=$DBDIR/WAL_LOG --sync=$SYNC --key_size=$KEYSIZE --write_buffer_size=$WRITE_BUFF_SIZE --num=$NUM"
        mkdir -p $RESULTS

        cd $PREDICT_LIB_DIR
        $PREDICT_LIB_DIR/compile.sh &> compile.out
        cd $DBHOME
        $DBHOME/db_bench $PARAMS $WRITEARGS #&> $RESULTS/WARMUP-WRITE.out

        ##Condition the DB to get Stable results
        $DBHOME/db_bench $PARAMS $READARGS  #&> $RESULTS/WARMUP-READ1.out
        FlushDisk
        #$DBHOME/db_bench $PARAMS $READARGS  &> WARMUP-READ2.out
}



GEN_RESULT_PATH() {
        WORKLOAD=$1
        CONFIG=$2
        THREAD=$3
        #WORKLOAD="DUMMY"
        #RESULTFILE=""
        RESULTS=$OUTPUTDIR/$APPOUTPUTNAME/$WORKLOAD/$THREAD
        mkdir -p $RESULTS
        RESULTFILE=$RESULTS/$CONFIG.out
}


RUN() {
        for NUM in "${num_arr[@]}"
        do
                CLEAR_DATA
                echo "BEGINNING TO WARM UP ......."
                COMPILE_AND_WRITE
                echo "FINISHING WARM UP ......."
                echo "..................................................."
                FlushDisk
                sudo dmesg -c
                exit
                for THREAD in "${thread_arr[@]}"
                do
                        PARAMS="--db=$DBDIR --value_size=$VALUE_SIZE --wal_dir=$DBDIR/WAL_LOG --sync=$SYNC --key_size=$KEYSIZE --write_buffer_size=$WRITE_BUFF_SIZE --num=$NUM"

                        for CONFIG in "${config_arr[@]}"
                        do
                                for WORKLOAD in "${workload_arr[@]}"
                                do
                                        RESULTS=""
                                        READARGS="--benchmarks=$WORKLOAD --use_existing_db=1 --mmap_read=0 --threads=$THREAD"
                                        GEN_RESULT_PATH $WORKLOAD $CONFIG $THREAD

                                        mkdir -p $RESULTS

                                        echo "RUNNING $CONFIG and writing results to #$RESULTS/$CONFIG.out"
                                        echo "..................................................."
                                        export LD_PRELOAD=/usr/lib/lib_$CONFIG.so
                                        $APPPREFIX "./"$APP $PARAMS $READARGS &> $RESULTFILE
                                        export LD_PRELOAD=""
                                        sudo dmesg -c &>> $RESULTFILE
                                        echo ".......FINISHING $CONFIG......................"
                                        FlushDisk
                                done
                        done
                done
        done
}

RUN
#CLEAR_DATA
exit

