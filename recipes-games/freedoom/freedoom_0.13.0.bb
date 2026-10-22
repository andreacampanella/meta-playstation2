SUMMARY = "Freedoom: Phase 1, free game data for Doom engines"
HOMEPAGE = "https://freedoom.github.io"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://COPYING.txt;md5=8fc7d0c4df94d13fa3802cf4bc9da382"

SRC_URI = "https://github.com/freedoom/freedoom/releases/download/v${PV}/freedoom-${PV}.zip"
SRC_URI[sha256sum] = "3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59"

S = "${WORKDIR}/freedoom-${PV}"

inherit allarch

do_configure[noexec] = "1"
do_compile[noexec] = "1"

# Doom engines look for game data in this directory. The file is 29 MiB.
do_install() {
    install -d ${D}${datadir}/games/doom
    install -m 0644 ${S}/freedoom1.wad ${D}${datadir}/games/doom/freedoom1.wad
}

FILES:${PN} = "${datadir}/games/doom"
