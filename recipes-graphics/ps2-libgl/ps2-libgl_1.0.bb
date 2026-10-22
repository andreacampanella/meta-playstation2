SUMMARY = "OpenGL 1.1 (fixed function subset) for the PlayStation 2: libGL.so.1"
DESCRIPTION = "Vector unit 1 transforms, clips and fogs the vertices and \
the Graphics Synthesizer draws, through the kernel device /dev/ps2gs. The \
VU1 program source is vu1.vsm, assembled into vu1.h by tools/vuasm.py. The package has \
libGL.so.1, libGLU.so.1 (the matrix helpers, and gluBuild2DMipmaps as a \
plain texture load), the GL/gl.h and GL/glu.h headers and the demos \
ps2gl-cubes and ps2gl-world. \
Programs load it as a normal OpenGL library. The SDL2 driver uses it for \
windows with the OpenGL flag."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ps2gl.c;subdir=src \
           file://vu1.h;subdir=src \
           file://hudfont.h;subdir=src \
           file://vu1.vsm;subdir=src \
           file://ps2gl-cubes.c;subdir=src \
           file://ps2gl-world.c;subdir=src \
           file://GL/gl.h;subdir=src \
           file://GL/glu.h;subdir=src \
           file://GL/glext.h;subdir=src \
           file://glu.c;subdir=src \
          "

S = "${WORKDIR}/src"

DEPENDS = "ps2gs-headers"

COMPATIBLE_MACHINE = "ps2"
PACKAGE_ARCH = "${MACHINE_ARCH}"

do_configure[noexec] = "1"

# PS2_PROFILE = "1" (classes/ps2-profile.bbclass) also builds the library
# as an archive, and libGL.so becomes a linker script that links the
# archive. A game built in the same mode then has the library inside, and
# its profile shows the library's functions: gprof samples the program
# only, and a shared library is outside it. The link lines of the games
# stay as they are. libGL.so.1 is still there for the programs that load
# it at run time.
inherit ps2-profile

# No stack protector in the library: a canary load and check in each
# function with an array, on a path that runs for every vertex.
SECURITY_STACK_PROTECTOR = ""

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -I${S} -fPIC -shared -Wl,-soname,libGL.so.1 \
        -o ${B}/libGL.so.1 ${S}/ps2gl.c -lm
    ln -sf libGL.so.1 ${B}/libGL.so
    ${CC} ${CFLAGS} ${LDFLAGS} -I${S} -fPIC -shared -Wl,-soname,libGLU.so.1 \
        -o ${B}/libGLU.so.1 ${S}/glu.c -L${B} -lGL -lm
    ln -sf libGLU.so.1 ${B}/libGLU.so
    if [ "${PS2_PROFILE}" = "1" ]; then
        ${CC} ${CFLAGS} -I${S} -fPIC -c -o ${B}/ps2gl-static.o ${S}/ps2gl.c
        rm -f ${B}/libGL.a
        ${AR} rcs ${B}/libGL.a ${B}/ps2gl-static.o
    fi
    for demo in ps2gl-cubes ps2gl-world; do
        ${CC} ${CFLAGS} ${LDFLAGS} -I${S} -o ${B}/$demo ${S}/$demo.c \
            -L${B} -lGL -lm
    done
}

do_install() {
    install -d ${D}${bindir} ${D}${libdir} ${D}${includedir}/GL
    install -m 0755 ${B}/ps2gl-cubes ${D}${bindir}/ps2gl-cubes
    install -m 0755 ${B}/ps2gl-world ${D}${bindir}/ps2gl-world
    install -m 0755 ${B}/libGL.so.1 ${D}${libdir}/libGL.so.1
    if [ "${PS2_PROFILE}" = "1" ]; then
        install -m 0644 ${B}/libGL.a ${D}${libdir}/libGL.a
        printf 'GROUP ( -l:libGL.a -lm )\n' > ${D}${libdir}/libGL.so
    else
        ln -sf libGL.so.1 ${D}${libdir}/libGL.so
    fi
    install -m 0755 ${B}/libGLU.so.1 ${D}${libdir}/libGLU.so.1
    ln -sf libGLU.so.1 ${D}${libdir}/libGLU.so
    install -m 0644 ${S}/GL/gl.h ${D}${includedir}/GL/gl.h
    install -m 0644 ${S}/GL/glu.h ${D}${includedir}/GL/glu.h
    install -m 0644 ${S}/GL/glext.h ${D}${includedir}/GL/glext.h
}

FILES:${PN} += "${libdir}/libGL.so.1 ${libdir}/libGLU.so.1"
FILES:${PN}-dev += "${libdir}/libGL.so ${libdir}/libGLU.so ${includedir}/GL"
