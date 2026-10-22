SUMMARY = "Sound on the PlayStation 2: the ALSA driver of the SPU2"
DESCRIPTION = "The kernel module snd-ps2 gives ALSA one playback device, \
48 kHz 16-bit stereo. The IOP module spu2 (ps2-iopmod) drives the sound \
processor through the sound library of the console ROM."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

inherit module

# The protocol header lives with the IOP module.
FILESEXTRAPATHS:prepend := "${THISDIR}/../../recipes-bsp/ps2-iopmod/files:"

SRC_URI = "file://Makefile \
           file://snd-ps2.c \
           file://spu2-sif.h \
           file://asound.conf \
"

S = "${WORKDIR}"

COMPATIBLE_MACHINE = "ps2"
PACKAGE_ARCH = "${MACHINE_ARCH}"

# The kernel option SND_PS2 (patch 0015) builds the ALSA core as modules.
RDEPENDS:${PN} += "kernel-module-snd-ps2 ps2-iopmod kernel-module-snd kernel-module-snd-pcm kernel-module-snd-timer kernel-module-soundcore"

# The module loads from the inittab (recipes-core/busybox), after the root
# switch: the init of this image reads no modules-load.d.

do_install:append() {
    install -d ${D}${sysconfdir}
    install -m 0644 ${WORKDIR}/asound.conf ${D}${sysconfdir}/asound.conf
}

FILES:${PN} += "${sysconfdir}/asound.conf"
