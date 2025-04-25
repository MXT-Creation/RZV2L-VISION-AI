# linux-renesas_5.10.bbappend

# Install the kernel module
do_install_append() {
	install -d ${D}/lib/modules/${KERNEL_VERSION}/extra
	install -m 0644 ${PKGD}/lib/modules/${KERNEL_VERSION}/kernel/lib/crypto/libarc4.ko \
		${D}/lib/modules/${KERNEL_VERSION}/extra/libarc4.ko
	install -m 0644 ${PKGD}/lib/modules/${KERNEL_VERSION}/kernel/net/wireless/cfg80211.ko \
		${D}/lib/modules/${KERNEL_VERSION}/extra/cfg80211.ko
	install -m 0644 ${PKGD}/lib/modules/${KERNEL_VERSION}/kernel/net/mac80211/mac80211.ko \
		${D}/lib/modules/${KERNEL_VERSION}/extra/mac80211.ko
}

KERNEL_MODULE_AUTOLOAD += "libarc cfg80211 mac80211"
KERNEL_MODULE_PROBECONF += "libarc cfg80211 mac80211"

FILES_${PN} += "/lib/modules/${KERNEL_VERSION}/extra/*.ko"