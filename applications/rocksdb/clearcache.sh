#/bin/bash
#set -x

FlushDisk()
{
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
	#echo "FINISHED CACHE CLEARING AFTER PREALLOCATION"
}

FlushDisk
