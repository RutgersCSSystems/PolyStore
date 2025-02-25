#!/bin/bash 

set -x

sudo apt-get -y install libssl-dev libelf-dev libncurses-dev libdpkg-dev flex bison #kernel-package

# Get kernel from Linux Kernel Archive
cd $KERNEL
git clone https://github.com/NVSL/linux-nova.git

cd $KERNEL_SRC
#sudo cp def.config .config
#make oldconfig
make menuconfig
scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS

export CONCURRENCY_LEVEL=$PARA

sudo make $PARA
sudo make modules
sudo make INSTALL_MOD_STRIP=1 modules_install
sudo make install

set +x
