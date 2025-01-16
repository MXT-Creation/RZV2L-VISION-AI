INITRAMFS_SCRIPTS = "\
	initramfs-framework-base \
	initramfs-module-setup-live \
	initramfs-module-udev \
"
require recipes-core/images/core-image-minimal-initramfs.bb

export IMAGE_BASENAME = "${MLPREFIX}core-image-emmc-flash"

PACKAGE_INSTALL += " \
	emmc-flash \
	kernel-image \
	kernel-devicetree \
"
