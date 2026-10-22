SUMMARY = "Network and SSH start script for the PlayStation 2"
DESCRIPTION = "Loads the driver of a USB network adapter, gets an address \
with DHCP and starts the dropbear SSH server. BusyBox init starts the script."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ps2-net;subdir=src \
           file://ps2-smap.conf;subdir=src \
          "

S = "${WORKDIR}/src"

# Address for a direct cable to a PC, or a network with no DHCP server.
PS2_NET_FALLBACK_ADDRESS ?= "192.168.2.2/24"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${S}/ps2-net ${D}${sbindir}/ps2-net
    sed -i -e 's|@FALLBACK@|${PS2_NET_FALLBACK_ADDRESS}|' ${D}${sbindir}/ps2-net
    install -d ${D}${sysconfdir}/modprobe.d
    install -m 0644 ${S}/ps2-smap.conf ${D}${sysconfdir}/modprobe.d/ps2-smap.conf
}

RDEPENDS:${PN} = "busybox busybox-udhcpc dropbear"
