SUMMARY = "The X desktop of the PlayStation 2: ps2-x starts X, the matchbox panel and window manager"
DESCRIPTION = "ps2-x finds the keyboard and the mouse (the init has no udev), \
writes them into the X configuration, and starts X with the matchbox window \
manager, the matchbox panel and a terminal, or with one command: ps2-x xeyes."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ps2-x \
           file://session \
          "

S = "${WORKDIR}"

inherit features_check
REQUIRED_DISTRO_FEATURES = "x11"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${bindir} ${D}${sysconfdir}/ps2-x
    install -m 0755 ${WORKDIR}/ps2-x ${D}${bindir}/ps2-x
    install -m 0755 ${WORKDIR}/session ${D}${sysconfdir}/ps2-x/session
}

RDEPENDS:${PN} = "xserver-xorg xf86-video-fbdev xf86-input-evdev xinit \
    matchbox-wm matchbox-panel-2 rxvt-unicode ttf-bitstream-vera fontconfig-utils"
