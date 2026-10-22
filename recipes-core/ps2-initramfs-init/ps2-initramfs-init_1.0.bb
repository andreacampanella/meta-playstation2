SUMMARY = "Start script for the PlayStation 2 initramfs"
DESCRIPTION = "Loads the USB and hard disk kernel modules. Then switches to \
a root file system on the hard disk or a USB drive, or starts BusyBox init \
from RAM."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# The sources go in their own directory. With S = "${WORKDIR}", a second build
# stops with a pseudo "path mismatch" error, because the package step makes
# hard links to the source files.
SRC_URI = "file://init;subdir=src"

S = "${WORKDIR}/src"

PACKAGE_ARCH = "${MACHINE_ARCH}"
COMPATIBLE_MACHINE = "ps2"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}/newroot
    install -m 0755 ${S}/init ${D}/init
    sed -i \
        -e 's|@MODULES@|${PS2_INITRAMFS_MODULES}|' \
        -e 's|@DISK_MODULES@|${PS2_INITRAMFS_EXTRA_MODULES}|' \
        -e 's|@ROOT@|${PS2_ROOT_DEVICE}|' \
        -e 's|@SWAP@|${PS2_SWAP_DEVICE}|' \
        -e 's|@WAIT@|${PS2_ROOT_WAIT}|' \
        ${D}/init
}

FILES:${PN} = "/init /newroot"
RDEPENDS:${PN} = "busybox"
