#!/bin/sh

#
# Usage ./emmc_flash.sh [optional rootfs.tar.gz or rootfs.tar.bz2 file]
#   Will create 2 rootfs partitions of 2.5 GB, and remainder of disk will be
#    a disk that can be used for various common data
#   If a file is specified, it will untar it to both rootfs partitions.
#   If no file is specified, it will clone the current SD-card disk to
#    both rootfs partitions on the eMMC
#

SDCARD_DISK_NUM=1
SDCARD_DISK=/dev/mmcblk${SDCARD_DISK_NUM}
ROOTFS_IMAGE="$1"

EMMC_DISK_NUM=0
EMMC_DISK=/dev/mmcblk${EMMC_DISK_NUM}

echo "=================================================================="
echo "| WARNING! WARNING! WARNING! WARNING! WARNING! WARNING! WARNING! |"
echo "|                                                                |"
echo "| This will erase all files on partitions of '$EMMC_DISK'"
echo "| Press Enter to continue.........                               |"
echo "| Ctrl + C to cancel.........                                    |"
echo "|                                                                |"
if [ -f "$ROOTFS_IMAGE" ] ; then
	echo "| Will untar file '$ROOTFS_IMAGE' to eMMC"
else
	echo "| Will clone SD-card to eMMC"
fi
echo "|                                                                |"
echo "| WARNING! WARNING! WARNING! WARNING! WARNING! WARNING! WARNING! |"
echo "=================================================================="
read ans

# eMMC is 15758000128 bytes; 2.5 GB per rootfs should be enough
# rest for persistent data between rootfs partitions
ROOTFS_SIZE=2500
SZ_SUFFIX=M

EXT_TYPE=ext4
ROOTFS1_PART_LABEL=rootfs1
ROOTFS2_PART_LABEL=rootfs2
USER_PART_LABEL=user

UBOOT_ENV_FILE=/home/root/u-boot.env

check_required_files_exist() {
	for file in $UBOOT_ENV_FILE ; do
		if [ ! -f $file ] ; then
			echo "== File '$file' not found; entering /bin/sh"
			exit 1
		fi
	done
}

mount_sysfs_if_needed() {
	if [ -e /sys/block/ ] ; then
		return 0
	fi

	echo "== Mounting sysfs =="
	/bin/mount -t sysfs sysfs /sys
}

emmc_destroy_partitions_if_any() {
	echo "== Destroying old partitions (if any) =="
	for pn in 1 2 3 ; do
		echo -e "\nd\n${pn}\n"\
			"w\n" | /sbin/fdisk -u ${EMMC_DISK}
	done
}

emmc_create_new_partitions() {
	echo "== Creating partition layout: 2 x rootfs (size ${ROOTFS_SIZE}${SZ_SUFFIX}) + remainder up to 15758000128 bytes =="
	echo -e "n\np\n1\n\n+${ROOTFS_SIZE}${SZ_SUFFIX}\n"\
		"n\np\n2\n\n+${ROOTFS_SIZE}${SZ_SUFFIX}\n"\
		"n\np\n3\n\n\n"\
		"p\nw\n" | /sbin/fdisk -u ${EMMC_DISK}

	for pn in 1 2 3 ; do
		part=${EMMC_DISK}p${pn}
		echo "== Clearing any potential old partition data for '${part}' =="
		/usr/sbin/wipefs -a $part
	done
}

emmc_format_ext4_to_partitions() {
	echo "== Formatting partitions as ${EXT_TYPE} =="
	/sbin/mkfs.${EXT_TYPE} -F -L $ROOTFS1_PART_LABEL ${EMMC_DISK}p1
	/sbin/mkfs.${EXT_TYPE} -F -L $ROOTFS2_PART_LABEL ${EMMC_DISK}p2
	/sbin/mkfs.${EXT_TYPE} -F -L $USER_PART_LABEL ${EMMC_DISK}p3
}

emmc_update_both_rootfs_partitions() {
	SDCARD_MNT=/tmp/sdcard
	EMMC_MNT=/tmp/emmc

	mkdir -p $SDCARD_MNT
	mkdir -p ${EMMC_MNT}
	/bin/mount ${SDCARD_DISK}p1 ${SDCARD_MNT} ||
		/bin/mount -o ro ${SDCARD_DISK}p1 ${SDCARD_MNT}
	for pn in 1 2 ; do
		part=${EMMC_DISK}p${pn}
		/bin/mount ${part} ${EMMC_MNT}

		echo "== Removing files on '$part' and copying from SD-card =="
		if [ ! -f "${ROOTFS_IMAGE}" ] ; then
			echo "== Copying SD-card contents to eMMC partition ${part} - will take a few miutes =="
			cp -r ${SDCARD_MNT}/* ${EMMC_MNT}
		else
			echo "== Untaring file '$ROOTFS_IMAGE' to eMMC partiton $part -will take a few minutes =="
			if echo ${ROOTFS_IMAGE} | grep -q tar.bz2 ; then
				/usr/bin/pv ${ROOTFS_IMAGE} | /bin/tar -C ${EMMC_MNT} -xj
			else
				/usr/bin/pv ${ROOTFS_IMAGE} | /bin/tar -C ${EMMC_MNT} -xz
			fi
		fi

		/bin/umount ${EMMC_MNT}
	done
	/bin/umount $SDCARD_MNT
}

# Note: UN-TESTED; this was adapted from RZG2L (where it works)
#echo "== Reprogramming BL2 and FIP =="
#echo "== onto eMMC boot partition 0... =="
#echo 0 > /sys/block/mmcblk0boot0/force_ro  # disable the soft write-protect base
#/bin/dd if=${BL2_IMAGE} of=/dev/mmcblk0boot0 bs=512 skip=0 seek=1
#/bin/dd if=${FIP_IMAGE} of=/dev/mmcblk0boot0 bs=512 skip=0 seek=256

update_uboot_env_from_file() {
	echo "== Resetting environment variables =="
	echo 0 > /sys/block/mmcblk0boot1/force_ro  # disable the soft write-protect base
	/usr/bin/fw_setenv --script ${UBOOT_ENV_FILE}

	echo "== Printing environment variables.. =="
	/usr/bin/fw_printenv
}

# The main program flow here

check_required_files_exist
mount_sysfs_if_needed
emmc_destroy_partitions_if_any
emmc_create_new_partitions
emmc_format_ext4_to_partitions
emmc_update_both_rootfs_partitions
update_uboot_env_from_file

/bin/sync
echo "== Done =="

