IMAGE_FEATURES_remove = " ssh-server-dropbear"
IMAGE_FEATURES_append = " ssh-server-openssh"

IMAGE_INSTALL_append = " \
	emmc-flash \
	u-boot-tools \
	kernel-image \
	kernel-devicetree \
	htop \
	da16600 \
"
