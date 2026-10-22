SUMMARY = "Duke Nukem 3D (the JFDuke3D port)"
DESCRIPTION = "JFDuke3D with its software renderer. The source code has no \
change for the PlayStation 2: the game uses SDL2 for picture and input. \
The game needs a data file, DUKE3D.GRP, from the shareware or the full \
version. Put it in ${datadir}/games/jfduke3d or in the directory where you \
start the game."
HOMEPAGE = "http://www.jonof.id.au/jfduke3d"
LICENSE = "GPL-2.0-only & BUILDLIC"
LIC_FILES_CHKSUM = "file://GPL.TXT;md5=8b2da277227a33a539463fe8bcfa0b11 \
                    file://jfbuild/buildlic.txt;md5=68b498942a59b13f60086049f5711ab7"
NO_GENERIC_LICENSE[BUILDLIC] = "jfbuild/buildlic.txt"

# The engine, the audio library and the control library are git submodules.
SRC_URI = "gitsm://github.com/jonof/jfduke3d.git;protocol=https;branch=master \
           file://duke3d.cfg \
           file://duke3d \
          "
# Head of master on 2026-10-13.
SRCREV = "55c5f9592d2a78e427a2bbdb13ce3c4c9ccf6f04"
PV = "0.0+git"

S = "${WORKDIR}/git"

DEPENDS = "libsdl2"

inherit pkgconfig ps2-profile ps2-game

# Software renderer only: the console has no OpenGL that the game can use.
# The makefiles ask "sdl2-config" for the SDL2 flags. pkg-config gives the
# same answers.
EXTRA_OEMAKE = "\
    USE_POLYMOST=0 USE_OPENGL=0 USE_ASM=0 WITHOUT_GTK=1 RELEASE=1 \
    'SDL2CONFIG=pkg-config sdl2' PKGCONFIG=pkg-config \
    DATADIR=${datadir}/games/jfduke3d \
    'CC=${CC}' 'CXX=${CXX}' \
"
# "debug" holds the optimization of the makefiles, and comes after
# CFLAGS: the level of classes/ps2-game.bbclass goes there. A release
# build also omits the frame pointer, which gprof (-pg) refuses, so the
# profiling build keeps it.
EXTRA_OEMAKE += "${@"'debug=${PS2_GAME_OPT}'" if d.getVar('PS2_PROFILE') == '1' else "'debug=-fomit-frame-pointer ${PS2_GAME_OPT}'"}"

do_configure[noexec] = "1"

do_compile() {
    oe_runmake veryclean || oe_runmake clean || true

    # bin2c runs during the build, so it is a program for the build machine.
    # Build it after the clean, which can delete it: make would then build
    # it with the cross compiler, and the PC cannot run that program.
    ${BUILD_CXX} -o ${S}/jfbuild/bin2c ${S}/jfbuild/tools/bin2c.cc

    # git holds rsrc/game.png and rsrc/game.bmp, and a checkout gives both
    # the time of the checkout, in either order. When the png is newer,
    # make converts it again with ImageMagick of the build machine, if it
    # has one. The bmp in git is the right one, so make it the newer file.
    touch ${S}/rsrc/*.bmp

    oe_runmake duke3d
}

do_install() {
    install -d ${D}${bindir} ${D}${libexecdir}/jfduke3d ${D}${datadir}/games/jfduke3d ${D}${ROOT_HOME}/.jfduke3d
    install -m 0755 ${S}/duke3d ${D}${libexecdir}/jfduke3d/duke3d
    install -m 0755 ${WORKDIR}/duke3d ${D}${bindir}/duke3d
    install -m 0644 ${WORKDIR}/duke3d.cfg ${D}${ROOT_HOME}/.jfduke3d/duke3d.cfg

    # The game does not search the current directory. It searches its data
    # directory, so a link there points to the file on the FAT partition.
    ln -s /boot/DUKE3D.GRP ${D}${datadir}/games/jfduke3d/duke3d.grp
}

INSANE_SKIP:${PN} += "symlink-to-sysroot"
FILES:${PN} += "${libexecdir}/jfduke3d ${datadir}/games/jfduke3d ${ROOT_HOME}/.jfduke3d"
