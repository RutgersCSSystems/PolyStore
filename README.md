## PolyStore: Exploiting Combined Capabilities of Heterogeneous Storage

This repository contains the artifact for reproducing our FAST '25 paper "PolyStore: Exploiting Combined Capabilities of Heterogeneous Storage".

## Table of Contents
* [Overview](##overview)
* [Setup](##setup)
* [Running Experiments: (Quick Tests)](##running-experiments)
* [Run Experiments and Generating Results](##generating-results)
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
    ├── resultgen/             # Experiment scripts for generating all data points (WIP)
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

**NOTE: If you are using our provided machine for AE, we have cloned the code and installed the kernel for you. The repo path is `/localhome/aereview`, you can directly go to Step 4.**

### Step 1: Get PolyStore source code on Github and clone it to your own working directory (Reviewer A, B, C, D)

```
$ mkdir /localhome/aereview/reviewX/
$ cd /localhome/aereview/reviewX/
$ git clone https://github.com/ingerido/polystore-artifact
```

### Step 2: Install required libraries and set environment variables for PolyStore

```
$ cd polystore-artifact
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
$ cd /localhome/aereview/polystore-artifact
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

## Running Experiments: (Quick Tests)

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


## Run Experiments and Generating Results

For the benchamrks and applicaions, we have provided running scripts in batch to match them with the major results in the paper figures in evaluation sections. Given the following limitations, we are not able to provide all of the graphs.

1) Most breakdown and analyses figures require manual check by injecting code into the target systems to gather the counters and desired metrics, which we believe is not practical for the artifact evaluation process.
2) Config II with SATA SSD is currently not available in the testing machine, hence we also excluded them.
3) The Intel Optane persistent memory module sufferred from malfunction issues, we replaced it and also the NVMe device as well. For all the evaluation results, it may not reflect the exact number in the paper, but we believe the overall trend (e.g., PolyStore provides roughly similar speedup than other systems) can be observed.
4) We only add Orthus and SPFS in this evaluation process because they are the most stable ones and can be put into batch running scripts, while others suffer from frequent kernel panic and require complex manual check up and fix.

### Single Device and PolyStore

Make sure the kernel version is the default 5.1.0+, 

```
$ uname -r
```

If not, use the following script to switch to the default 5.1.0+ and reboot

```
$ cd $BASE
$ ./scripts/grub_set_kernel_spfs.sh
$ ./scripts/reboot_hard.sh
```

After reboot, make sure the kernel version is desired (5.1.0+)

```
$ uname -r
```

When running single device (PM-only and NVMe-only) or PolyStore, make sure the desired file systems are mounted properly (**Step 6** in the **Environment** section).

```
$ cd $BASE/resultgen/figX
$ ./run_pmonly.sh
$ ./run_nvmeonly.sh
$ ./run_polystore.sh
```

### SPFS

When running SPFS, we need to switch to another kernel version (5.1.0+SPFS). 

```
$ uname -r
```

If the kernel version is not desired, switch to 5.1.0+SPFS with the following scripts and reboot

```
$ cd $BASE
$ ./scripts/grub_set_kernel_spfs.sh
$ ./scripts/reboot_hard.sh
```

After reboot, make sure the kernel version is desired (5.1.0+SPFS)

```
$ uname -r
```

Then mount the SPFS with the following command:

```
$ cd $BASE
$ ./scripts/setup_spfs.sh           # type 'y' when mkfs prompts
```

After SPFS mounted successfully, you will see the following: (You can see /mnt/spfs is mounted twice, because it is a stackable file system where a standalone SPFS file system is mounted on top of the backend device with ext4)

```
/mnt/spfs         /dev/nvme1n1p1   ext4        rw,relatime
  └─/mnt/spfs     /mnt/spfs        spfs        rw,relatime,pmem=/dev/pmem0,mode=tiering,migr_fsync_interval=1000, ...
```

Run SPFS: (If you encounter kernel panic in RocksDB YCSB, you can try reboot the system (check the **Rebooting System if Necessary** section). I only succeeded to run it once in trying maybe 10 times. Feel free to skip. SPFS sometimes does not run in some of our experiments due to its bugs, I cannot do much about it >_< )

```
$ cd $BASE/resultgen/figX
$ ./run_spfs.sh
```

### Orthus

When running Orthus, we need to switch to another kernel version (5.4.0-150-generic) because Orthus is built on top of OpenCAS block cache framework and can only be compiled and work on this kernel version from what we run.

```
$ uname -r
```

If the kernel version is not desired, switch to 5.4.0-150-generic with the following scripts and reboot

```
$ cd $BASE
$ ./scripts/grub_set_kernel_orthus.sh
$ ./scripts/reboot_hard.sh
```

After reboot, make sure the kernel version is desired (5.4.0-150-generic)

```
$ uname -r
```

Then mount the Orthus with the following command:

```
$ cd $BASE
$ ./scripts/setup_orthus.sh           # type 'y' when mkfs prompts
```

After Orthus mounted successfully, you will see the following: (You can see /mnt/orthus is mounted with /dev/cas1 - the virtual device with block cache setup)

```
/mnt/orthus           /dev/cas1             OPENCAS          rw,relatime,mode=755,uid=0,gid=0
```

Run Orthus: (If you encounter kernel panic in Orthus, Please reboot the system (check the **Rebooting System if Necessary** section). Orthus sometimes does not run in some of our experiments due to its bugs, I cannot do much about it >_< )

```
$ cd $BASE/resultgen/figX
$ ./run_orthus.sh
```

### Extracting results

After finish all of them, run the following result extraction scripts and it will extract all results in a human-readable format:
```
$ ./extract_results.sh
```

## Rebooting System if Necessary

### Soft reboot

When switching between the kernels, please use the following script to reboot the system in a softway

```
$ cd $BASE
$ ./scripts/reboot_soft.sh
```

### Hard reboot

When a kernel panic or soft lockup happened, please use the following script to reboot the system in a hard way

```
$ cd $BASE
$ ./scripts/reboot_hard.sh
```


## Known issues

0. The PolyOS may incur software lock up and make the benchmarks or application hanging. (Solved)

2. The system may show the following error information shows up: (Solved)
```PolyStore ERROR: Failed to map inode region```

We recommend reviewers using our *sysreset* script,
```
# Navigate to the artifact's root folder
$ cd /localhome/aereview/polystore-artifact
$ sudo scripts/sysreset.sh   
```
After rebooting, as mentioned in step 4 above, make sure to set the environmental variable again.

2. Filebench may hang after printing the result. Press Ctrl+D to kill the process.

