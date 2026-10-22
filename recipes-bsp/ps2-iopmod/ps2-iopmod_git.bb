SUMMARY = "PlayStation 2 input/output processor (IOP) modules"
DESCRIPTION = "Firmware for the IOP. The kernel loads these IRX files from \
/lib/firmware/ps2. The irqrelay module sends IOP interrupts to the kernel. \
The USB host driver needs it."
HOMEPAGE = "https://github.com/frno7/iopmod"
LICENSE = "GPL-2.0-only & MIT"
LIC_FILES_CHKSUM = "file://licence/GPL-2.0;md5=e6a75371ba4d16749254a51215d13f97 \
                    file://licence/MIT;md5=e8f57dd048e186199433be2c41bd3d6d"

SRC_URI = "git://github.com/frno7/iopmod.git;protocol=https;branch=main \
           file://spu2.c \
           file://libsd.h \
           file://spu2-module.h \
           file://spu2-sif.h \
"
# Head of main on 2026-10-13.
SRCREV = "12cdf14b78ed5e648ffe1d52e6754f00b3b69001"
PV = "1.0+git"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "ps2"
PACKAGE_ARCH = "${MACHINE_ARCH}"

# The makefile builds host tools with CC and IOP code with TARGET_CC.
# The IOP code uses soft float. The glibc headers then ask for a stubs
# header that a hard-float sysroot does not have. An empty file is sufficient,
# because the IOP code does not link with glibc.
EXTRA_OEMAKE = "\
    V=1 \
    'CC=${BUILD_CC}' \
    'AR=${BUILD_AR}' \
    'LDFLAGS=${BUILD_LDFLAGS}' \
    'CFLAGS=-I${WORKDIR}/stubs' \
    'CROSS_COMPILE=${TARGET_PREFIX}' \
    'TARGET_CC=${TARGET_PREFIX}gcc --sysroot=${STAGING_DIR_TARGET}' \
"
PARALLEL_MAKE = ""

# The layer adds the sound module spu2 to the tree: its source, its module
# header (name and version, which the IOP linker checks), the import header
# of the ROM library LIBSD, and the protocol header that the kernel driver
# snd-ps2 shares (recipes-kernel/ps2-sound).
do_configure() {
    install -d ${WORKDIR}/stubs/gnu
    : > ${WORKDIR}/stubs/gnu/stubs-o32_soft.h
    install -m 0644 ${WORKDIR}/spu2.c ${S}/module/spu2.c
    install -m 0644 ${WORKDIR}/libsd.h ${S}/include/iopmod/module/libsd.h
    install -m 0644 ${WORKDIR}/spu2-module.h ${S}/include/iopmod/module/spu2.h
    install -m 0644 ${WORKDIR}/spu2-sif.h ${S}/include/iopmod/spu2-sif.h
}

do_compile() {
    oe_runmake module
}

do_install() {
    install -d ${D}${nonarch_base_libdir}/firmware/ps2
    install -m 0644 ${S}/module/*.irx ${D}${nonarch_base_libdir}/firmware/ps2/
}

FILES:${PN} = "${nonarch_base_libdir}/firmware/ps2"

# The IRX files are relocatable objects for the IOP. Do not strip or split them.
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INHIBIT_SYSROOT_STRIP = "1"
INSANE_SKIP:${PN} = "arch ldflags textrel already-stripped file-rdeps"
