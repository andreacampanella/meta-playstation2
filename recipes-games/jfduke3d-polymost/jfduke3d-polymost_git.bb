SUMMARY = "Duke Nukem 3D (JFDuke3D, 2018) with the Polymost OpenGL renderer"
DESCRIPTION = "The JFDuke3D version of April 2018 is the last one with the \
fixed function OpenGL renderer (Polymost). Later versions need OpenGL 2.0 \
shaders, which the Graphics Synthesizer cannot run. The source code has no \
change for the PlayStation 2: the game uses SDL2 for picture and input and \
loads libGL.so.1 at run time. The command is duke3d-gl. The game needs \
DUKE3D.GRP on the FAT partition, as the software version does."
HOMEPAGE = "http://www.jonof.id.au/jfduke3d"
LICENSE = "GPL-2.0-only & BUILDLIC"
LIC_FILES_CHKSUM = "file://GPL.TXT;md5=8b2da277227a33a539463fe8bcfa0b11 \
                    file://jfbuild/buildlic.txt;md5=68b498942a59b13f60086049f5711ab7"
NO_GENERIC_LICENSE[BUILDLIC] = "jfbuild/buildlic.txt"

# The engine, the audio library and the control library are git submodules
# of the game. The 2018 game commit points to an audio library commit that
# is no longer on a branch, so the recipe fetches the four repositories
# itself. The audio library commit of the same day on master differs only
# in Windows and macOS build files.
# sdlappicon_game.c is the window icon. The build makes it with a tool that
# runs on the build machine and needs the engine library, so the recipe
# gives the generated file.
SRC_URI = "git://github.com/jonof/jfduke3d.git;protocol=https;nobranch=1;name=game;destsuffix=git \
           git://github.com/jonof/jfbuild.git;protocol=https;nobranch=1;name=jfbuild;destsuffix=git/jfbuild \
           git://github.com/jonof/jfmact.git;protocol=https;nobranch=1;name=jfmact;destsuffix=git/jfmact \
           git://github.com/jonof/jfaudiolib.git;protocol=https;nobranch=1;name=jfaudiolib;destsuffix=git/jfaudiolib \
           file://duke3d.cfg \
           file://duke3d-gl \
           file://sdlappicon_game.c \
          "
# 2018-04-12: the last game commit before the shader renderer.
SRCREV_game = "d3473f1b325b9c52eadabde506386bc4b3a7bbad"
SRCREV_jfbuild = "bf6d6931ed58efdcf421c7c3b75dba9fad906ccd"
SRCREV_jfmact = "bd42ccb107ef1d2b0eefaeca493917aef51334a7"
SRCREV_jfaudiolib = "c5f6256b97e9164edb328246b76d4dda816167e8"
SRCREV_FORMAT = "game_jfbuild_jfmact_jfaudiolib"
PV = "2018.04+git"

S = "${WORKDIR}/git"

DEPENDS = "libsdl2 ps2-libgl"

inherit pkgconfig ps2-profile ps2-fastdf ps2-game

# Polymost with OpenGL loaded at run time (through SDL2). The makefiles ask
# "sdl2-config" for the SDL2 flags. pkg-config gives the same answers. The
# link step uses CXXFLAGS and no LDFLAGS, so the linker flags go there.
EXTRA_OEMAKE = "\
    USE_OPENGL=1 POLYMOST=1 DYNAMIC_OPENGL=1 NOASM=1 WITHOUT_GTK=1 RELEASE=1 \
    'SDL2CONFIG=pkg-config sdl2' \
    'CC=${CC}' 'CXX=${CXX}' 'CFLAGS=${CFLAGS}' 'CXXFLAGS=${CXXFLAGS} ${LDFLAGS}' \
"
# "debug" holds the optimization of the makefiles, and comes after
# CFLAGS: the level of classes/ps2-game.bbclass goes there. A release
# build also omits the frame pointer, which gprof (-pg) refuses, so the
# profiling build keeps it.
EXTRA_OEMAKE += "${@"'debug=${PS2_GAME_OPT}'" if d.getVar('PS2_PROFILE') == '1' else "'debug=-fomit-frame-pointer ${PS2_GAME_OPT}'"}"


# The 2018 source has format strings that are not literals. The compiler
# option of Poky makes them errors.
SECURITY_STRINGFORMAT = ""

do_configure() {
    # The icon tool is a program for the build machine that needs the engine
    # library. A script with the same name prints the generated file instead.
    printf '#!/bin/sh\ncat %s\n' ${WORKDIR}/sdlappicon_game.c > ${S}/jfbuild/generatesdlappicon
    chmod 755 ${S}/jfbuild/generatesdlappicon
    cp ${WORKDIR}/sdlappicon_game.c ${S}/rsrc/sdlappicon_game.c
    touch ${S}/rsrc/sdlappicon_game.c
}

do_compile() {
    # A build after a change of the flags (PS2_PROFILE) must not keep the
    # engine objects of the one before.
    oe_runmake veryclean || oe_runmake clean || true
    oe_runmake duke3d
}

do_install() {
    install -d ${D}${bindir} ${D}${libexecdir}/jfduke3d-polymost \
        ${D}${datadir}/games/jfduke3d-polymost
    install -m 0755 ${S}/duke3d ${D}${libexecdir}/jfduke3d-polymost/duke3d
    install -m 0755 ${WORKDIR}/duke3d-gl ${D}${bindir}/duke3d-gl
    install -m 0644 ${WORKDIR}/duke3d.cfg ${D}${datadir}/games/jfduke3d-polymost/duke3d.cfg

    # The game searches its data directory, so a link there points to the
    # file on the FAT partition.
    ln -s /boot/DUKE3D.GRP ${D}${datadir}/games/jfduke3d-polymost/duke3d.grp
}

INSANE_SKIP:${PN} += "symlink-to-sysroot"
FILES:${PN} += "${libexecdir}/jfduke3d-polymost ${datadir}/games/jfduke3d-polymost"
RDEPENDS:${PN} += "ps2-libgl"
