#!/bin/bash 

USER=$(whoami)
DEFAULT_SSD_DEVICE="/dev/nvme1n1"
DEFAULT_SSD_PARTITION="/dev/nvme1n1p1"
#DEFAULT_SSD_DEVICE="/dev/sdc"
#DEFAULT_SSD_PARTITION="/dev/sdc1"
DEFAULT_MOUNTPOINT=$SLOW_DIR
SSD_DEVICE=${1:-$DEFAULT_SSD_DEVICE}
SSD_PARTITION=${2:-$DEFAULT_SSD_PARTITION}
MOUNTPOINT=${3:-$DEFAULT_MOUNTPOINT}

echo $USER

if [ $(mount | grep -c $MOUNTPOINT) != 1 ]; then
        if [ ! -d $MOUNTPOINT ]; then
                sudo mkdir $MOUNTPOINT
	fi
	sudo mount $SSD_PARTITION $MOUNTPOINT
	if [ $? -eq 0 ]; then
		echo "Already partitioned"
	else
		#sudo fdisk $SSD_DEVICE
		sudo mkfs.ext4 $SSD_PARTITION
		sudo mount $SSD_PARTITION $MOUNTPOINT
	fi

	sudo chown -R $USER $MOUNTPOINT

	echo "$MOUNTPOINT is now mounted"
else
	echo "$MOUNTPOINT already mounted"
fi

sudo chown -R $USER $MOUNTPOINT
