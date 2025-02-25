#!/bin/bash

set_kernel () {
    if [ -z "$1" ]; then
        echo "Error: Kernel version is empty."
        exit 1
    fi

    new_ent="GRUB_DEFAULT=\"1>$1\""
    echo $new_ent
    sleep 5
    cp /etc/default/grub grub.bk
    sudo sed -i 's/^GRUB_DEFAULT=.*/'"$new_ent"'/' /etc/default/grub
    sudo update-grub
}

set_kernel 18
sleep 5
#sudo reboot
