#!/bin/bash -e

#
# Usage ./populate_sdcard.sh <sd-card-device> [Yocto-Deploy-Dir]
#

DEFAULT_YOCTO_DIR="build/tmp/deploy/images/smarc-rzv2l"
YOCTO_DEPLOY_DIR="${2:-${DEFAULT_YOCTO_DIR}}"

usage_check() {
	if [ -z "$1" ] ; then
		echo "No SD card device provided"
		echo "Usage: $0 <sd-card-device> [Yocto-Deploy-Dir - defaults to '$DEFAULT_YOCTO_DIR']"
		exit 1
	fi
	if [ ! -e "$1" ] ; then
		echo "SD-card '$1' device does not exist"
		exit 1
	fi
	if [[ $1 != /dev/sd* ]] && [[ $1 != /dev/mmcblk* ]] ; then
		echo "SD-card '$1' device must be named '/dev/sdX' or '/dev/mmcblkX'"
		exit 1
	fi
	if [ ! -d "$2" ] ; then
		echo "Yocto deploy directory '$2' does not exist"
		exit 1
	fi
}

usage_check "$1" "$YOCTO_DEPLOY_DIR"

WORK_DIR="${YOCTO_DEPLOY_DIR}/build/sd_card"

ROOTFS_IMG_FILE="${ROOTFS_IMG_FILE:-core-image-bsp-smarc-rzv2l.tar.bz2}"
ROOTFS_IMG_FILE="$YOCTO_DEPLOY_DIR/$ROOTFS_IMG_FILE"

untar_roofs() {
	local dst="$1"

	echo "Unpacking rootfs file '$ROOTFS_IMG_FILE'"

	sudo rm -rf ${dst}/*
	sudo tar -xf "$ROOTFS_IMG_FILE" -C "$dst"
}

populate_sd_card() {
	local devname="$1"
	local PSUF
	local TMP="/tmp"

	local mount_dir1="${TMP}/mount_work1/"

	mkdir -p ${mount_dir1}

	if [[ $devname == /dev/mmcblk* ]] ; then
		PSUF=p
	fi

	echo == Unmounting partitions first ==
	sudo umount ${devname}${PSUF}1 &> /dev/null || true

	# Populate rootfs
	echo "== Populating rootfs partition '${devname}${PSUF}1' =="
	sudo mount ${devname}${PSUF}1 ${mount_dir1}
	untar_roofs "${mount_dir1}"

	echo "== Syncing... =="

	sudo sync

	echo == Unmounting partitions \(almost done\) ==
	sudo umount ${devname}${PSUF}1

	echo "== Done... =="
}

echo "=================================================================="
echo "| WARNING! WARNING! WARNING! WARNING! WARNING! WARNING! WARNING! |"
echo "|                                                                |"
echo "| This will erase all files on partitions of '$1'"
echo "| Press Enter to continue.........                               |"
echo "|                                                                |"
echo "| WARNING! WARNING! WARNING! WARNING! WARNING! WARNING! WARNING! |"
echo "=================================================================="
read ans

populate_sd_card "$1"
