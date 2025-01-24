require include/core-image-sdk.inc
require include/extend_packages.inc

IMAGE_INSTALL_append = " \
        kernel-module-mmngr \
        kernel-module-mmngrbuf \
        mmngrbuf-user-module \
        mmngr-user-module \
"


TOOLCHAIN_TARGET_TASK_append = " drpai "

