#!/bin/bash

set_env () {
  set -x
  BCACHE_MNT=$PWD/bcache
  BCACHE_ID=bcache0
  BCACHE_DEV=/dev/$BCACHE_ID
  BCACHE_FS=ext4
  
  BACKING_IMG=$PWD/NVMe.img
  BACKING_SIZE=128g
  
  CACHE_DEV=/dev/pmem0p1
  
  INFO_LOOP=$PWD/.bcache-loop
  INFO_CACHE_ID=$PWD/.bcache-cacheid
  
  CACHE_MODE=writeback
  CACHE_POLICY=lru
  set +x
}

bcache_debug () {
  echo "[bcache.sh][$FUNC] $1"
}

bcache_error () {
  echo "[bcache.sh] (ERROR) $1"
}

bcache_print_usage () {
  echo "Usage:"
  echo " - create  (create bcache device)"
  echo " - destroy (destroy bcache device)"
  echo " - flush"
  echo " - reset"
}

bcache_exists () {
  stat $BCACHE_DEV &> /dev/null
  return $?
}

bcache_mounted () {
  mountpoint -q $BCACHE_MNT 
  return $?
}

get_cache_id () {
  cat $INFO_CACHE_ID
}

get_loop_dev () {
  cat $INFO_LOOP
}

valid_cache_id_info (){
  CACHE_ID=$(get_loop_dev)
  if [ -z "$CACHE_ID" ]; then
    return 1
  else
    return 0
  fi
}

valid_loop_info () {
  LOOP_DEV=$(get_loop_dev)
  if [ -z "$LOOP_DEV" ]; then
    return 1
  else
    return 0
  fi
}

valid_info () {
  if valid_cache_id_info; then
    if valid_loop_info; then 
    return 0
  fi
  return 1
else
  return 1
fi
return 1
}

mount_bcache () {
  bcache_debug "Creating bcache mount directory $BCACHE_MNT"
  rm -rf $BCACHE_MNT
  mkdir $BCACHE_MNT
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  bcache_debug "Mounting $BCACHE_DEV to $BCACHE_MNT"
  sudo mount $BCACHE_DEV $BCACHE_MNT
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
}

unmount_bcache () {
  bcache_debug "Unmounting $BCACHE_DEV from $BCACHE_MNT"
  sudo umount $BCACHE_MNT
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  bcache_debug "Removing bcache mount directory $BCACHE_MNT"
  rm -rf $BCACHE_MNT
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
}

bcache_create () {
  
  # set debug function
  FUNC=create
  
  # check if mounted 
  if bcache_mounted; then
    bcache_error "Seems like $BCACHE_DEV is mounted at $BCACHE_MNT. Unmount before destroying."
    exit 1
  fi
  
  # check if bcache device already exists
  if bcache_exists; then
    bcache_error "Seems like $BCACHE_DEV already exists. You need to destroy it first."
    exit 1
  fi
  
  # create backing device image 
  bcache_debug "Creating backing device image $BACKING_IMG"
  sudo qemu-img create $BACKING_IMG $BACKING_SIZE
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # finding open loop device 
  bcache_debug "Finding open loop device"
  sudo losetup -f > $INFO_LOOP
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  if valid_loop_info; then
    LOOP_DEV=$(cat $INFO_LOOP)
  else
    bcache_error "Failed to write loop info to file. Terminating."
    exit 1
  fi
  
  # retierate loop device that will be used 
  bcache_debug "Found loop device $LOOP_DEV"
  
  # loop backing device image to loop device 
  bcache_debug "Looping backing device image $BACKING_IMG to $LOOP_DEV"
  sudo losetup -o 0 $LOOP_DEV $BACKING_IMG
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # make bcache device
  bcache_debug "Making bcache device with $LOOP_DEV (CACHE_MODE=$CACHE_MODE)"
  sudo make-bcache -B $LOOP_DEV --$CACHE_MODE
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # make bcache cache device 
  bcache_debug "Making bcache cache device with $CACHE_DEV (CACHE_POLICY=$CACHE_POLICY)"
  sudo make-bcache -C $CACHE_DEV --cache_replacement_policy=$CACHE_POLICY 
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # store bcach cache device id to file 
  bcache_debug "Storing cache device id to $INFO_CACHE_ID"
  sudo bcache-super-show $CACHE_DEV | grep cset | sed -n -e 's/^.*cset.uuid\t\t//p' > $INFO_CACHE_ID
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  if valid_cache_id_info; then
    CACHE_ID=$(cat $INFO_CACHE_ID)
  else
    bcache_error "Failed to write cache id info to file. Terminating."
    exit 1
  fi
  
  # register cache device to bcache device 
  bcache_debug "Registering cache device $CACHE_DEV ($CACHE_ID) to $BCACHE_ID"
  sudo su --c "echo $CACHE_ID > /sys/block/$BCACHE_ID/bcache/attach"
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # format bcache device
  bcache_debug "Formating bcache device $BCACHE_DEV to $BCACHE_FS"
  sudo mkfs.$BCACHE_FS $BCACHE_DEV
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # mount bcache devie 
  mount_bcache
  
  bcache_debug "Bcache creation done."
}

bcache_destroy () {
  
  # set debug function
  FUNC=destroy
  
  # check if bcache device already exists
  if bcache_exists; then
    :
  else
    bcache_error "Seems like $BCACHE_DEV already doesn't exist."
    exit 1
  fi
  
  if valid_cache_id_info; then
    :
  else
    bcache_error "Could not fetch cache id from $INFO_CACHE_ID. Quitting."
    exit 1
  fi
  
  if valid_loop_info; then
    :
  else
    bcache_error "Could not fetch loop device info from $INFO_LOOP. Quitting."
    exit 1
  fi
  
  # check if mounted 
  if bcache_mounted; then
    bcache_debug "Seems like $BCACHE_DEV is mounted at $BCACHE_MNT. Going to unmount"
    unmount_bcache
  fi
  
  bcache_debug "Going to destroy $BCACHE_DEV"
  
  # fetch cache device id 
  bcache_debug "Getting cache device ID from $INFO_CACHE_ID"
  CACHE_ID=$(get_cache_id)
  
  # reiterate cache device id
  bcache_debug " Found Cache device ID: $CACHE_ID"
  
  # unregister cache device 
  bcache_debug "Unregistering Cache Device $CACHE_DEV ($CACHE_ID)"
  sudo su --c "echo 1 > /sys/fs/bcache/$CACHE_ID/unregister"
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # remove cache id info file 
  bcache_debug "Removing Cache ID file $INFO_CACHE_ID"
  sudo rm $INFO_CACHE_ID
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # stop and destroy bcache device
  bcache_debug "Destroying bcache device $BCACHE_DEV"
  sudo su --c "echo 1 > /sys/block/$BCACHE_ID/bcache/stop"
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # clear the cache device 
  bcache_debug "Clearing $CACHE_DEV"
  sudo wipefs -a $CACHE_DEV
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # fetch loop device info from file
  bcache_debug "Fetching loop device info from $INFO_LOOP"
  LOOP_DEV=$(get_loop_dev)
  
  # clear the backing device 
  bcache_debug "Clearing backing device $LOOP_DEV"
  sudo wipefs -a $LOOP_DEV
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # destroy loop device
  bcache_debug "Destroying backing device $LOOP_DEV"
  sudo losetup -d $LOOP_DEV
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # remove loop device info file 
  bcache_debug "Removing loop device info file $INFO_LOOP"
  sudo rm $INFO_LOOP
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # destroy backing device image
  bcache_debug "Destroying backing device image $BACKING_IMG"
  sudo rm $BACKING_IMG
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  bcache_debug "Bcache destruction done."
}


bcache_flush () {
  FUNC=flush
  
  if bcache_mounted; then
    :
  else
    bcache_error "Seems like $BCACHE_DEV is not mounted at $BCACHE_MNT. Terminating."
  fi
  
  bcache_debug "Flushing cache to backing device"
  PERCENT=$(cat /sys/block/$BCACHE_ID/bcache/writeback_percent)
  
  bcache_debug "Original writeback threshold is $PERCENT%"
  
  bcache_debug "Flushing cache by setting threshold to 0%"
  sudo su --c "echo 0 > /sys/block/$BCACHE_ID/bcache/writeback_percent"
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  bcache_debug "Waiting for 1 minute to let all dirty cache be flushed."
  sleep 1m
  bcache_debug "Minute over. Hopefully all the dirty cache pages has been flushed."
  
  bcache_debug "Setting writeback threshold back to $PERCENT%"
  sudo su --c "echo $PERCENT > /sys/block/$BCACHE_ID/bcache/writeback_percent"
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  bcache_debug "Cache flush done."
  
}

bcache_reset () {
  FUNC=reset
  
  if valid_cache_id_info; then
    :
  else
    bcache_error "Unable to get cache device id. Terminating."
    exit 1
  fi
  
  # fetch cache device id 
  bcache_debug "Getting cache device ID from $INFO_CACHE_ID"
  CACHE_ID=$(get_cache_id)
  
  # reiterate cache device id
  bcache_debug "Found Cache device ID: $CACHE_ID"
  
  # we need to flush before we try and replace the cache drive
  bcache_flush
  FUNC=reset

  # detatch cache drive
  bcache_debug "Detaching cache device $CACHE_DEV ($CACHE_ID) from $BCACHE_ID"
  sudo su --c "echo $CACHE_ID > /sys/block/$BCACHE_ID/bcache/detach"
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # unregister cache device 
  bcache_debug "Unregistering Cache Device $CACHE_DEV ($CACHE_ID)"
  sudo su --c "echo 1 > /sys/fs/bcache/$CACHE_ID/unregister"
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # clear the cache device 
  bcache_debug "Clearing $CACHE_DEV"
  sudo wipefs -a $CACHE_DEV
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # remove cache id info file 
  bcache_debug "Removing Cache ID file $INFO_CACHE_ID"
  sudo rm $INFO_CACHE_ID
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # make bcache cache device 
  bcache_debug "Making bcache cache device with $CACHE_DEV (CACHE_POLICY=$CACHE_POLICY)"
  sudo make-bcache -C $CACHE_DEV --cache_replacement_policy=$CACHE_POLICY 
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  # store bcache cache device id to file 
  bcache_debug "Storing cache device id to $INFO_CACHE_ID"
  sudo bcache-super-show $CACHE_DEV | grep cset | sed -n -e 's/^.*cset.uuid\t\t//p' > $INFO_CACHE_ID
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  if valid_cache_id_info; then
    CACHE_ID=$(cat $INFO_CACHE_ID)
  else
    bcache_error "Failed to write cache id info to file. Terminating."
    exit 1
  fi
  
  # register cache device to bcache device 
  bcache_debug "Registering cache device $CACHE_DEV ($CACHE_ID) to $BCACHE_ID"
  sudo su --c "echo $CACHE_ID > /sys/block/$BCACHE_ID/bcache/attach"
  if [ $? -ne 0 ]; then
    bcache_error "Operation failed. Terminating."
    exit 1
  fi
  
  bcache_debug "Cache reset done."
}


if [ $# -eq 1 ]; then
  if [ $1 == "create" ]; then
    set_env
    bcache_create
  elif [ $1 == "destroy" ]; then
    set_env
    bcache_destroy
  elif [ $1 == "flush" ]; then
    set_env
    bcache_flush
  elif [ $1 == "reset" ]; then
    set_env
    bcache_reset
  else
    bcache_error "Unknown command"
    bcache_print_usage
  fi
else
  bcache_error "Incorrect # of arguments"
  bcache_print_usage
fi
