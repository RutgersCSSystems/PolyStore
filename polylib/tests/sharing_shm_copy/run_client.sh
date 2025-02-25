#!/bin/bash

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./interval-tree/:./libs/libshmht/

./client
