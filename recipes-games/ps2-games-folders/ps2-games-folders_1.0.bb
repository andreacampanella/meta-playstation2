SUMMARY = "The game data folders in the home directory of root"
DESCRIPTION = "Makes ~/games/duke3d, ~/games/quake/id1, ~/games/descent \
and ~/games/doom, each with a README.txt that names the data files that go \
there. The games look in these folders first, then in /data and on the \
FAT partition (/boot)."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit allarch

do_fetch[noexec] = "1"
do_unpack[noexec] = "1"
do_patch[noexec] = "1"
do_configure[noexec] = "1"
do_compile[noexec] = "1"

GAMES = "${ROOT_HOME}/games"

do_install() {
    install -d ${D}${GAMES}/duke3d ${D}${GAMES}/quake/id1 \
        ${D}${GAMES}/descent ${D}${GAMES}/doom
    echo "Put DUKE3D.GRP here, from the shareware or the full game." \
        > ${D}${GAMES}/duke3d/README.txt
    echo "Put pak0.pak (shareware) and pak1.pak (full game) in id1/ here." \
        > ${D}${GAMES}/quake/README.txt
    echo "Put descent.hog and descent.pig here, shareware 1.4 or full game." \
        > ${D}${GAMES}/descent/README.txt
    echo "Put doom1.wad, doom.wad or doom2.wad here. Without one, Freedoom starts." \
        > ${D}${GAMES}/doom/README.txt
}

FILES:${PN} = "${GAMES}"
