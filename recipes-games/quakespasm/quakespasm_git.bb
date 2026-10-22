SUMMARY = "Quake (QuakeSpasm) with OpenGL on the GS"
DESCRIPTION = "QuakeSpasm is the maintained Quake 1 engine. It draws with \
OpenGL 1.1 fixed function calls through libGL.so.1, and uses SDL2 for the \
picture and the input. The source code has no change for the PlayStation 2. \
The command is quake. The game needs the data of the shareware or the full \
game in /boot/id1 on the FAT partition."
HOMEPAGE = "https://quakespasm.sourceforge.net"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=570a9b3749dd0463a1778803b12a6dce"

SRC_URI = "git://github.com/sezero/quakespasm.git;protocol=https;branch=master \
           file://quake \
          "
# Head of master on 2026-10-03.
SRCREV = "c9c8ee9895961669c4674e8f8ca8c64cc43e38b5"
PV = "0.97+git"

S = "${WORKDIR}/git"

DEPENDS = "libsdl2 ps2-libgl"

# fastdf: the double arithmetic on the FPU. The profile of the start map had 22 ms a frame in the software
# doubles. Quake keeps its clock as a double of seconds since the start:
# with 24 bits the step is 1 ms after three hours of play, which the
# frame timing does not notice.
inherit pkgconfig ps2-profile ps2-fastdf ps2-game

# No music codecs (the memory). The makefile strips the
# program, and the package step wants to do that itself (STRIP=true). It takes extra
# compiler flags in CPUFLAGS, and it asks "sdl2-config" for the SDL2 flags.
EXTRA_OEMAKE = "\
    -C ${S}/Quake USE_SDL2=1 USE_CODEC_MP3=0 USE_CODEC_VORBIS=0 \
    'SDL_CONFIG=pkg-config sdl2' 'CC=${CC}' 'LDFLAGS=${LDFLAGS}' STRIP=true \
"


# The makefile adds -O2 after the flags from the environment, and the last
# -O wins: the level of classes/ps2-game.bbclass goes in its place.
do_configure() {
    sed -i -e 's/^CFLAGS += -O2$/CFLAGS += ${PS2_GAME_OPT}/' ${S}/Quake/Makefile
}

do_compile() {
    oe_runmake clean
    oe_runmake quakespasm
}

do_install() {
    install -d ${D}${bindir} ${D}${libexecdir}/quakespasm
    install -m 0755 ${S}/Quake/quakespasm ${D}${libexecdir}/quakespasm/quakespasm
    install -m 0755 ${WORKDIR}/quake ${D}${bindir}/quake
}

FILES:${PN} += "${libexecdir}/quakespasm"
RDEPENDS:${PN} += "ps2-libgl"
