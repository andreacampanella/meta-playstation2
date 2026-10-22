SUMMARY = "Doom for the PlayStation 2 under Linux"
DESCRIPTION = "The doomgeneric port with platform code for the PlayStation 2. \
The Graphics Synthesizer scales the 320x200 picture to the screen. Input \
comes from USB keyboards. The wrapper finds the game data file (IWAD) in \
~/games/doom, or starts Freedoom."
HOMEPAGE = "https://github.com/ozkl/doomgeneric"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://LICENSE;md5=b234ee4d69f5fce4486a80fdaf4a4263"

SRC_URI = "git://github.com/ozkl/doomgeneric.git;protocol=https;branch=master \
           file://doomgeneric_ps2.c;subdir=git/doomgeneric \
           file://ps2-doom \
          "
# Head of master on 2026-10-13.
SRCREV = "e257a09527062bfb986b6e5e0780839b6cbd5b71"
PV = "1.0+git"

S = "${WORKDIR}/git"

inherit ps2-profile ps2-game

COMPATIBLE_MACHINE = "ps2"
PACKAGE_ARCH = "${MACHINE_ARCH}"

# The source list of Makefile.linuxvt, with the PlayStation 2 platform code.
DOOM_SOURCES = "\
    dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop \
    d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus \
    i_endoom i_joystick i_scale i_sound i_system i_timer memio m_argv m_bbox \
    m_cheat m_config m_controls m_fixed m_menu m_misc m_random p_ceilng \
    p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats \
    p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user \
    r_bsp r_data r_draw r_main r_plane r_segs r_sky r_things sha1 sounds \
    statdump st_lib st_stuff s_sound tables v_video wi_stuff w_checksum \
    w_file w_main w_wad z_zone w_file_stdc i_input i_video doomgeneric \
    mus2mid \
"

DOOM_CFLAGS = "-DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE \
    -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -w"

do_configure[noexec] = "1"

do_compile() {
    # Some game data files have the fourth episode but not its menu picture
    # (M_EPI4). The game then stops when the player selects "New Game". Hide
    # the fourth menu entry if the picture is not there.
    sed -i -e 's/if (gameversion < exe_ultimate)$/if (gameversion < exe_ultimate || W_CheckNumForName("M_EPI4") < 0)/' \
        ${S}/doomgeneric/m_menu.c

    objs=""
    for f in ${DOOM_SOURCES}; do
        ${CC} ${CFLAGS} ${DOOM_CFLAGS} -c ${S}/doomgeneric/$f.c -o ${B}/$f.o
        objs="$objs ${B}/$f.o"
    done
    ${CC} ${CFLAGS} ${DOOM_CFLAGS} \
        -c ${S}/doomgeneric/doomgeneric_ps2.c -o ${B}/doomgeneric_ps2.o
    ${CC} ${CFLAGS} ${LDFLAGS} -o ${B}/ps2-doom $objs ${B}/doomgeneric_ps2.o -lm
}

# The wrapper ps2-doom finds the game data (~/games/doom first) and starts
# the program in libexec.
do_install() {
    install -d ${D}${bindir} ${D}${libexecdir}/ps2-doom
    install -m 0755 ${B}/ps2-doom ${D}${libexecdir}/ps2-doom/ps2-doom
    install -m 0755 ${WORKDIR}/ps2-doom ${D}${bindir}/ps2-doom
}
