SUMMARY = "Compressed swap in RAM (zram) for the PlayStation 2"
DESCRIPTION = "Starts a zram swap device at boot. Swapped pages stay in RAM \
in compressed form, at memory speed, where the USB drive is slow. The size \
is PS2_ZRAM_SIZE (MiB) in the recipe, 24 by default."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ps2-zram"

PS2_ZRAM_SIZE ?= "24"

COMPATIBLE_MACHINE = "ps2"
PACKAGE_ARCH = "${MACHINE_ARCH}"
RDEPENDS:${PN} = "kernel-module-zram busybox"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/ps2-zram ${D}${sbindir}/ps2-zram
    sed -i -e 's|@SIZE@|${PS2_ZRAM_SIZE}|' ${D}${sbindir}/ps2-zram
}
