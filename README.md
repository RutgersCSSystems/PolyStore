## PolyStore: Exploiting Combined Capabilities of Heterogeneous Storage

This repository contains the artifact for our FAST '25 paper "PolyStore: Exploiting Combined Capabilities of Heterogeneous Storage".

## Table of Contents
* [Overview](##overview)
* [Setup](##setup)
* [Running Experiments](##running-experiments)
* [Rebooting System if Necessary](##rebooting-system)
* [Known Issues](##known-issues)


## Overview 

### Directory structure

    ├── polylib/               # PolyStore library and PolyStore kernel module
        ├── libs/              # Necessary third-party libraries for PolyStore
        ├── src/               # Source code for PolyStore library and kernel module
    ├── tools/                 # Tools for PolyStore
    ├── scripts/               # Scripts for setting environments
    ├── kernel/                # Directory for building Linux 5.1.0+ (NOVA)
    ├── benchmarks/            # Benchmark workloads
    ├── applications/          # Application workloads
    ├── experiments/           # Experiment scripts for all benchmarks and applications
    ├── LICENSE
    └── README.md

### Environment: 

**Operating Systems**

Our artifact is based on **Linux kernel 5.1.0+** with NOVA file system. The current scripts are developed for **Ubuntu 20.04.5 LTS**. Porting to other Linux distributions would require some script modifications.

**Storage Hardwre**

We use the following two devices as our base hetegorgeneous storage device configuration
- 256GB Intel Optane persistent memory (/dev/pmem0) 
- 400GB Intel NVMe SSD (/dev/nvme1n1)

## Setup 

### Step 1: Get PolyStore source code on Github and clone it to your own working directory

```
$ git clone https://github.com/ingerido/PolyStore
```

### Step 2: Install required libraries and set environment variables for PolyStore

```
$ cd PolyStore
$ source scripts/setvars   # Install dependent packages and setup enviroment variables
```
NOTE: If you are prompted during Ubuntu package installation, please hit enter and all the package installation to complete.

### Step 3: Compile the kernel (Skip if you are using our lab machine)

On a Cloudlab machine, we need to install Linux 5.1.0+ kernel with NOVA file systems used in PolyStore in our paper.

NOTE:  If you are using our provided machine for AE, we have installed the kernel for you. You don't need to reinstall the kernel. 

```
$ cd $BASE
$ ./scripts/compile_kernel.sh
$ sudo reboot
```

After reboot, check the kernel version. It should be 5.1.0+

### Step 4: Set environmental variables and compile and install libraries

Please use `screen` to manage the terminal session and maintain the connection.

```
$ screen
$ cd <your_directory for the PolyStore repo>/PolyStore
$ source scripts/setvars.sh
$ cd $BASE/polylib
$ make clean
$ make
$ cd $BASE/polylib/src/polyos
$ make clean
$ make
$ cd $BASE/tools
$ make
$ cd $BASE
```

**Please note, if you get logged out of the SSH session or reboot the system (as mentioned below), you must repeat step 4 and set the environmental variable again before running.** 


### Step 5: Compile and build benchmarks and applications

Microbench

```
$ cd $BASE/benchmarks/microbench
$ make
```

Filebench

```
$ cd $BASE/benchmarks/filebench
$ ./build_filebench.sh
```

Redis

```
$ cd $BASE/applications/redis
$ ./build_redis.sh
```

RocksDB

```
$ cd $BASE/applications/rocksdb
$ ./build_rocksdb.sh
```

GraphWalker

```
$ cd $BASE/applications/graphwalker
$ make
```

### Step 6: Mount file systems for Heterogeneous Storage

First, check if the desired file systems are mounted
```
$ findmnt
```
If they are well-mounted, it will show:
```
/mnt/fast           /dev/pmem0             NOVA          rw,relatime,mode=755,uid=0,gid=0
/mnt/slow           /dev/nvme1n1p1         ext4          rw,relatime
```

If NOT, then mount the file systems for the desired storage devices
```
$ cd $BASE
$ ./scripts/mount_pmem_nova.sh
$ ./scripts/mount_nvme_ext4.sh
```

If successful, you will see the following:
```
/mnt/fast           /dev/pmem0             NOVA          rw,relatime,mode=755,uid=0,gid=0
/mnt/slow           /dev/nvme1n1p1         ext4          rw,relatime
```

## Running Experiments:

For the benchamrks and applicaions, we have separate running scripts for each approach listed in Table.3 in the paper

### 1. microbench

Expect output will be similar to ```aggregated thruput 7072.90 MB/s, average latency 32.08 us```. If you can see the above output, you are good for all necessary environmental settings. You can start running all other experiments for artifact evaluation.

```
$ cd $BASE/experiments/microbench
```

#### Run *PolyStore-static* 

```
$ ./run_polystore_static.sh
```

#### Run *PolyStore-dynamic* 

```
$ ./run_polystore_dynamic.sh
```

#### Run *PolyStore (w/ Poly-cache enabled)* 

```
$ ./run_polystore_polycache.sh
```

#### Run *PM-only (NOVA)* 

```
$ ./run_pmonly.sh
```

#### Run *NVMe-only (ext4)* 

```
$ ./run_nvmeonly.sh
```

### 2. Filebench

Expect output will be similar to ```IO Summary: 26330 ops 2622.160 ops/s 262/525 rd/wr 2103.1mb/s  19.9ms/op```. If you can see the above output, it means Filebench is working properly.

```
$ cd $BASE/experiments/filebench
```

#### Run *PolyStore (w/ Poly-cache enabled)* 

```
$ ./run_polystore_polycache.sh
```

#### Run *PM-only (NOVA)* 

```
$ ./run_pmonly.sh
```

#### Run *NVMe-only (ext4)* 

```
$ ./run_nvmeonly.sh
```

### 3. RocksDB

Expect output will be similar to ```IO Summary: 26330 ops 2622.160 ops/s 262/525 rd/wr 2103.1mb/s  19.9ms/op```. If you can see the above output, It means RocksDB YCSB is running properly.

```
$ cd $BASE/experiments/rocksdb
```

#### Run *PolyStore (w/ Poly-cache enabled)* 

```
$ ./run_polystore_polycache_ycsb.sh
```

#### Run *PM-only (NOVA)* 

```
$ ./run_pmonly_ycsb.sh
```

#### Run *NVMe-only (ext4)* 

```
$ ./run_nvmeonly_ycsb.sh
```

### 4. GraphWalker

Expect output will be show a breakdown starting with the title ``` === REPORT FOR multi-source-personalizedpagerank() ===```. If you can see this output, it means GraphWalker is working properly.

```
$ cd $BASE/experiments/graphwalker
```

#### Run *PolyStore (w/ Poly-cache enabled)* 

```
$ ./run_polystore_polycache.sh
```


## Known issues

0. The PolyOS may incur software lock up and make the benchmarks or application hanging. (Solved)

1. The system may show the following error information shows up: (Solved)
```PolyStore ERROR: Failed to map inode region```

Reboot the machine if it fails.

After rebooting, as mentioned in step 4 above, make sure to set the environmental variable again.

