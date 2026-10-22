# Login banner with the commands of this layer and the layer version.

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:ps2 = " file://ps2-motd"

do_install:append:ps2() {
    sed -e 's|@VERSION@|${PS2_LAYER_VERSION}|' ${WORKDIR}/ps2-motd > ${D}${sysconfdir}/motd
}
