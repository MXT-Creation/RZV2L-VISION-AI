SUMMARY = "ETB - Embedded ToolBox"
SECTION = "app"
LICENSE = "MIT&Apache&BSD-3-Clause"
LIC_FILES_CHKSUM = " \
"

inherit systemd
inherit cmake

SRC_URI = "file://src/ \
"

S = "${WORKDIR}/src"

DEPENDS += " drpai libjpeg-turbo libwebsockets json-c opencv "
RDEPENDS_${PN} += " drpai-models "

SYSTEMD_AUTO_ENABLE = "enable"
SYSTEMD_SERVICE_${PN} = "etb.service"

FILES_${PN} = " \
	${bindir} \
	${datadir} \
	${systemd_unitdir} \
"

