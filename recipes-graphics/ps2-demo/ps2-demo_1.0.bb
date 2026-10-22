SUMMARY = "Feature demo for the PlayStation 2 graphics: direct GS and OpenGL"
DESCRIPTION = "Two programs draw the same seven scenes. ps2demo-gs writes GIF \
packets to /dev/ps2gs itself, with the transform, the clipping and the fog on \
the CPU. ps2demo-gl draws through libGL.so.1 (VU1 and the GS). The pictures \
must match. Space goes to the next scene, Escape quits."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://demo.h;subdir=src \
           file://demo_main.h;subdir=src \
           file://demo_main.c;subdir=src \
           file://demo_scenes.c;subdir=src \
           file://demo_gl.c;subdir=src \
           file://demo_gs.c;subdir=src \
          "

S = "${WORKDIR}/src"

DEPENDS = "ps2gs-headers ps2-libgl"
RDEPENDS:${PN} = "ps2-libgl"

COMPATIBLE_MACHINE = "ps2"
PACKAGE_ARCH = "${MACHINE_ARCH}"

do_configure[noexec] = "1"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -I${S} -o ${B}/ps2demo-gs \
        ${S}/demo_gs.c ${S}/demo_scenes.c ${S}/demo_main.c -lm
    ${CC} ${CFLAGS} ${LDFLAGS} -I${S} -o ${B}/ps2demo-gl \
        ${S}/demo_gl.c ${S}/demo_scenes.c ${S}/demo_main.c -lGL -lm
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/ps2demo-gs ${D}${bindir}/ps2demo-gs
    install -m 0755 ${B}/ps2demo-gl ${D}${bindir}/ps2demo-gl
}
