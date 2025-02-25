#!/bin/bash

cd ../../
source scripts/setvars.sh
cd experiments/graphwalker

# Output result directory
result_dir=$RESULTS_PATH/graphwalker/polystore-test

# Setup parameters
GRAPHFILE=$ORTHUS_DIR/graph.txt

FlushDisk() {
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
        sudo sh -c "sync"
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

ResetFiles() {
	rm -rf $ORTHUS_DIR/.persist/
	rm -rf $ORTHUS_DIR/graph.txt
	rm -rf $ORTHUS_DIR/graph.txt_GraphWalker/
	rm -rf $ORTHUS_DIR/graph.txt
	rm -rf $ORTHUS_DIR/graph.txt_GraphWalker/
}

FlushDisk

# Copy Graph
ResetFiles
cp $GRAPHDATASET_PATH/com-friendster.ungraph.txt $GRAPHFILE

# Run benchmark
cd $GRAPHWALKER_PATH
#LOG_LEVEL="debug" ./bin/apps/msppr file $GRAPHFILE firstsource 0 numsources 2000 walkspersource 2000 maxwalklength 100 prob 0.2 execthreads 16
LOG_LEVEL="debug" ./bin/apps/msppr file $GRAPHFILE firstsource 0 numsources 20000 walkspersource 20000 maxwalklength 100 prob 0.2 execthreads 16
cd -

FlushDisk
sleep 2
