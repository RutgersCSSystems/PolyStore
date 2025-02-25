#!/bin/bash

LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../../libs/syscall-intercept/build/:../../libs/interval-tree/
export LD_LIBRARY_PATH
export INTERCEPT_LOG="./intercept_log"
export INTERCEPT_LOG_TRUNC=0

LD_PRELOAD=../../build/libpolystore.so ./test_shim
