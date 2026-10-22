SUMMARY = "The hard disk on the network adapter: APA partitions for Linux, or a data disk"
DESCRIPTION = "ps2-disk status shows the disks. ps2-disk apa-format formats \
the hard disk as APA, the format of the console software, uLaunchELF and \
OPL, with the partitions linux-root, linux-swap and +linux. ps2-disk \
install copies the running system to linux-root and the kernel to +linux, \
so uLaunchELF starts Linux from the hard disk. ps2-disk setup makes an MBR \
data disk instead (swap and /data), and ps2-disk mount (run by init at \
boot) starts its swap and mounts /data. PS2_DISK_SWAP_MIB sets the swap \
size, 256 by default. For APA it must be 128, 256, 512 or 1024."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ps2-disk"

PS2_DISK_SWAP_MIB ?= "256"

COMPATIBLE_MACHINE = "ps2"
PACKAGE_ARCH = "${MACHINE_ARCH}"
RDEPENDS:${PN} = "busybox e2fsprogs-mke2fs e2fsprogs-e2fsck pfsshell"
RRECOMMENDS:${PN} = "kernel-module-pata-ps2 kernel-module-iop-dev9"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/ps2-disk ${D}${sbindir}/ps2-disk
    sed -i -e 's|@SWAP@|${PS2_DISK_SWAP_MIB}|' ${D}${sbindir}/ps2-disk
}
