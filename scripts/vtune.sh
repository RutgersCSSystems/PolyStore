#!/bin/bash

VTUNE_DOWNLOAD_URL=http://registrationcenter-download.intel.com/akdlm/irc_nas/tec/15518/vtune_amplifier_2019_update4.tar.gz

VTUNE_INSTALLER_ARCHIVE=vtune_amplifier_2019_update4.tar.gz
VTUNE_INSTALLER_FOLDER=vtune_amplifier_2019_update4
VTUNE_INSTALLER_PATH=$VTUNE_INSTALLER_FOLDER/install.sh
VTUNE_DEFAULT_INSTALLATION_PATH=/opt/intel/vtune_amplifier

vtune_debug () {
	echo "[VTUNE.sh] $1"
}

vtune_error () {
	echo "[VTUNE.sh] (ERROR) $1"
}


check_prerequisites () {

	# Check for xorg
	which Xorg
	if [ $? -eq 0 ]; then
		vtune_debug "Found Xorg prerequisite"
	else
		vtune_debug "Could not find xorg prequisite"
		
		vtune_debug "Trying to install xorg"
		sudo apt-get install xorg

		if [ $? -eq 0 ]; then
			vtune_debug "Sucessfully installed Xorg"
		else
			vtune_debug "Failed to install Xorg"
			exit 1
		fi
	fi

	sudo apt-get install libnss3 libcanberra-gtk3-module libcanberra-gtk-module gnome-keyring

}

check_previous_vtune () {
	# Check for signs of installation
	ls $VTUNE_DEFAULT_INSTALLATION_PATH &> /dev/null

	if [ $? -eq 0 ]; then
		vtune_debug "Previously installation of VTUNE detected."
		exit 1
	fi
}

download_vtune () {

	vtune_debug "Downloading VTUNE Installer"
	wget $VTUNE_DOWNLOAD_URL

	if [ $? -ne 0 ]; then
		vtune_debug "Failed to download VTUNE installer"
		exit 1
	fi
}

extract_installer () {

	# unzip 
	vtune_debug "Extracting VTUNE Installer"
	tar -zxf $VTUNE_INSTALLER_ARCHIVE 

	if [ $? -ne 0 ]; then
		debug "Failed to extract VTUNE installer"
		exit 1
	fi


}	

install_vtune () {
	# run installer 
	vtune_debug "Running Installer"
	sudo $VTUNE_INSTALLER_PATH 

	if [ $? -ne 0 ]; then
		vtune_error "Failed to install VTUNE"
		exit 1
	fi
}

cleanup_installer () {
	
	# Delete intaller folder 
	rm -rf $VTUNE_INSTALLER_FOLDER

	if [ $? -ne 0 ]; then
		vtune_error "Could not delete installer folder"
	fi


	# Delete install archive
	rm $VTUNE_INSTALLER_ARCHIVE

	if [ $? -ne 0 ]; then
		vtune_error "Could not delete installer archive"
	fi

}

check_installation () {

	# Run installation self-checker
	vtune_debug "Checking VTUNE Installation. This will take a minute."
	cmdouput=$($VTUNE_DEFAULT_INSTALLATION_PATH/bin64/amplxe-self-checker.sh)

	if [ $? -ne 0 ]; then
		vtune_error "VTUNE Installation seems to have failed"
		vtune_error "Rerun the VTUNE self checker manually for more information"
		vtune_error "Location: ($VTUNE_DEFAULT_INSTALLATION_PATH/bin64/amplxe-self-checker.sh)"
		exit 1
	else
		vtune_debug "VTUNE Installation seems to be good"
	fi

}

post_installation () {

	# Run Post installtion script 
	#source /opt/intel/vtune_amplifier_2019.4.0.597835/amplxe-vars.sh

	# Ensuring installation directory is writable so vtune can be run a non-root 
	vtune_debug "Ensuring VTUNE installation directory permissions are set for non-root use"
	sudo chmod -R ug+rw /opt/intel

	if [ $? -ne  0 ]; then 
		echo "Could not set vtune installation directory permissions"
	fi

	# Set ptrace scope temporarily
	vtune_debug "Setting current ptrace scope to 0 for current session"
	sudo bash -c "echo 0 > /proc/sys/kernel/yama/ptrace_scope"

	if [ $? -ne 0 ]; then
		echo "Could not set the current ptrace scope"
	fi


	# Setting ptrace scope permenantly
	vtune_debug "Setting ptrace scope config to 0 permenantly"
	sudo sed -i "s/kernel.yama.ptrace_scope = 1/kernel.yama.ptrace_scope = 0/" /etc/sysctl.d/10-ptrace.conf

	if [ $? -ne 0 ]; then 
		echo "Could not set ptrace scope configuration"
	fi
	
	# Set kptr_restirct and perf_event_paranoid
	sudo bash -c 'echo "kernel.kptr_restrict=0" >> /etc/sysctl.conf'
	sudo bash -c 'echo "kernel.perf_event_paranoid=0" >> /etc/sysctl.conf'
	sudo sysctl -p /etc/sysctl.conf
	
}


vtune_install () {
	check_previous_vtune
	check_prerequisites
	download_vtune
	extract_installer
	install_vtune
	cleanup_installer
	post_installation
	check_installation
}

vtune_uninstall () {
	vtune_debug "Going to uninstall VTUNE"	
	sudo $VTUNE_DEFAULT_INSTALLATION_PATH/uninstall.sh	
	
	if [ $? -eq 0 ]; then 
		vtune_debug "Uninstalled VTUNE sucessfully"
	else
		vtune_error "Failed to uninstall VTUNE"
	fi
}

vtune_check () {
	:
}

vtune_selfcheck () {
	:
}

vtune_run () {
	sudo $VTUNE_DEFAULT_INSTALLATION_PATH/
}


#download_vtune
#extract_installer
#install_vtune
#post_installation
#check_installation

#check_previous_vtune

#cleanup_installer


if [ $# -eq 1 ]; then
	if [ $1 == "install" ]; then
		vtune_install
	elif [ $1 == "uninstall" ]; then
		vtune_uninstall
	elif [ $1 == "run" ]; then
		vtune_run
	else
		vtune_debug "Unknown Option"
		vtune_print_usage
	fi	
else
	vtune_print_usage
fi




