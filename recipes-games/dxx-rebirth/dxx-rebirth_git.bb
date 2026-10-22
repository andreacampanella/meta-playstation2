SUMMARY = "Descent 1 (D1X-Rebirth) with OpenGL on the GS"
DESCRIPTION = "DXX-Rebirth is the maintained Descent engine. It draws with \
OpenGL 1.1 fixed function calls and vertex arrays through libGL.so.1, with \
GLU for the perspective matrix, and uses SDL2 for the picture and the \
input. The source code has no change for the PlayStation 2. The command is \
descent. The game needs descent.hog and descent.pig of the shareware or \
the full game in /boot/descent on the FAT partition."
HOMEPAGE = "https://www.dxx-rebirth.com"
LICENSE = "GPL-3.0-only & DXX-Rebirth"
LIC_FILES_CHKSUM = "file://GPL-3.txt;md5=d32239bcb673463ab874e80d47fae504 \
                    file://COPYING.txt;md5=7077769a51d612f2edcc6abd1e5dfa6d"
NO_GENERIC_LICENSE[DXX-Rebirth] = "COPYING.txt"

SRC_URI = "git://github.com/dxx-rebirth/dxx-rebirth.git;protocol=https;branch=master \
           file://descent \
           file://descent.cfg \
          "
# 2026-06-28: the last commit that GCC 13 (Yocto scarthgap) compiles. The
# next commit needs the C++23 explicit object member function of GCC 14.
SRCREV = "3c0e6a23b6c0a6ca65d124b3e18a564b12a87a9c"
PV = "0.61.0+git"

S = "${WORKDIR}/git"

DEPENDS = "libsdl2 libsdl2-image physfs ps2-libgl python3-scons-native"

# SCons puts its -O2 before the CXXFLAGS from the environment, so the
# level of classes/ps2-game.bbclass wins with no change.
inherit pkgconfig ps2-profile ps2-game

# SCons reads CXX, CPPFLAGS, CXXFLAGS and LDFLAGS from the environment.
# No SDL_mixer: the sound effects go through SDL2 itself, and the music
# would need the codecs and their memory. No screenshots: no libpng.
# SDL2_image reads the PCX pictures (the menus, the briefings).
# words_need_alignment: the MIPS core traps on an unaligned load.
# The build takes some time: the engine is C++23 with many templates.
SCONS_OPTIONS = "\
    d1x=1 d2x=0 sdl2=1 sdlmixer=0 sdlimage=1 use_udp=1 use_tracker=0 ipv6=0 \
    screenshot=none opengl=1 words_need_alignment=1 lto=0 verbosebuild=0 \
    register_install_target=0 builddir=build/ \
"

do_configure[noexec] = "1"

do_compile() {
    export CXX="${CXX}"
    export CPPFLAGS="${CPPFLAGS}"
    export CXXFLAGS="${CXXFLAGS}"
    export LDFLAGS="${LDFLAGS}"
    scons ${PARALLEL_MAKE} ${SCONS_OPTIONS}
}

do_install() {
    install -d ${D}${bindir} ${D}${libexecdir}/dxx-rebirth ${D}${datadir}/games/dxx-rebirth
    install -m 0755 ${S}/build/d1x-rebirth/d1x-rebirth ${D}${libexecdir}/dxx-rebirth/d1x-rebirth
    install -m 0755 ${WORKDIR}/descent ${D}${bindir}/descent
    install -m 0644 ${WORKDIR}/descent.cfg ${D}${datadir}/games/dxx-rebirth/descent.cfg
}

FILES:${PN} += "${libexecdir}/dxx-rebirth ${datadir}/games/dxx-rebirth"
RDEPENDS:${PN} += "ps2-libgl"
