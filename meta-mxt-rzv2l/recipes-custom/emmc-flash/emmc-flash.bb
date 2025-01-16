SUMMARY = "Custom script to clone the SD-card to the eMMC and set it as a boot device"
SECTION = "custom"
LICENSE = "None"
LIC_FILES_CHKSUM = " \
"

RDEPENDS_${PN} = " \
	pv \
	rsync \
	util-linux \
	dosfstools \
	e2fsprogs-badblocks \
	e2fsprogs-dumpe2fs \
	e2fsprogs-e2fsck \
	e2fsprogs-e2scrub \
	e2fsprogs-mke2fs \
	e2fsprogs-resize2fs \
	e2fsprogs-tune2fs \
	mtd-utils \
	libubootenv-bin \
"

SRC_URI = " \
	file://emmc_flash.sh \
	file://fw_env.config \
	file://u-boot.env \
"

do_install() {
	install -m 0755 -d ${D}/home/root/
	install -m 0755 ${WORKDIR}/emmc_flash.sh ${D}/home/root/
	install -m 0755 ${WORKDIR}/u-boot.env ${D}/home/root/

	install -m 0755 -d ${D}/etc/
	install -m 0644 ${WORKDIR}/fw_env.config ${D}/etc/

}

FILES_${PN} = " \
	/etc/* \
	/home/root/* \
"
