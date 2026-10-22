SUMMARY = "Header of the PlayStation 2 Graphics Synthesizer device"
DESCRIPTION = "Installs linux/ps2gs.h, the interface of /dev/ps2gs. The \
kernel patch in recipes-kernel/linux/files has the same file."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

SRC_URI = "file://linux/ps2gs.h;subdir=src"

S = "${WORKDIR}/src"

COMPATIBLE_MACHINE = "ps2"
PACKAGE_ARCH = "${MACHINE_ARCH}"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${includedir}/linux
    install -m 0644 ${S}/linux/ps2gs.h ${D}${includedir}/linux/ps2gs.h
}

ALLOW_EMPTY:${PN} = "1"
