#!/bin/bash 

set -x

sudo insmod $POLYOS_PATH/polystore_controller.ko param_max_inode_nr=16384 param_max_it_node_nr=65536 param_fast_dir=/mnt/fast param_slow_dir=/mnt/slow

set +x
