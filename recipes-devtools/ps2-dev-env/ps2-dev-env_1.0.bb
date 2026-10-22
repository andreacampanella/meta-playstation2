SUMMARY = "Shell settings for building programs on the console"
DESCRIPTION = "/etc/profile.d/ps2-dev.sh: GCC writes its temporary files \
to ~/.cache/tmp instead of the RAM, and make runs one job at a time."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ps2-dev.sh;subdir=src"
S = "${WORKDIR}/src"

inherit allarch

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${sysconfdir}/profile.d
    install -m 0644 ${S}/ps2-dev.sh ${D}${sysconfdir}/profile.d/ps2-dev.sh
}
