SUMMARY = "Fast doubles for the R5900: the libgcc double routines on the FPU"
DESCRIPTION = "libfastdf.a defines the software double routines of libgcc \
(__muldf3, __adddf3, the conversions and the comparisons) as single \
precision operations on the FPU. A double keeps its size and its calling \
convention and loses precision: 24 bits instead of 53. A game recipe links \
it with 'inherit ps2-fastdf'."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://fastdf.c;subdir=src \
           file://fastdf-arith.S;subdir=src \
          "

S = "${WORKDIR}/src"

COMPATIBLE_MACHINE = "ps2"

do_configure[noexec] = "1"

do_compile() {
    ${CC} ${CFLAGS} -O2 -c -o ${B}/fastdf.o ${S}/fastdf.c
    ${CC} ${CFLAGS} -c -o ${B}/fastdf-arith.o ${S}/fastdf-arith.S
    rm -f ${B}/libfastdf.a
    ${AR} rcs ${B}/libfastdf.a ${B}/fastdf.o ${B}/fastdf-arith.o
}

do_install() {
    install -d ${D}${libdir}
    install -m 0644 ${B}/libfastdf.a ${D}${libdir}/libfastdf.a
}

# The archive goes to the sysroot for the games. Nothing goes to the image.
FILES:${PN}-staticdev = "${libdir}/libfastdf.a"

# ps2-image-dev installs ps2-fastdf-staticdev, which pulls ps2-fastdf-dev
# and ps2-fastdf. The main package has no files, so it must be allowed to
# be empty.
ALLOW_EMPTY:${PN} = "1"
