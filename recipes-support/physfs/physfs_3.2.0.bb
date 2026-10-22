SUMMARY = "PhysicsFS: a library for file access in games"
DESCRIPTION = "PhysicsFS gives a game one search path over directories and \
archive files (ZIP, HOG and others). DXX-Rebirth (Descent) needs it."
HOMEPAGE = "https://icculus.org/physfs/"
LICENSE = "Zlib"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=e64c08153665a18b56559d0683a64909"

SRC_URI = "git://github.com/icculus/physfs.git;protocol=https;nobranch=1"
# Tag release-3.2.0
SRCREV = "eb3383b532c5f74bfeb42ec306ba2cf80eed988c"

S = "${WORKDIR}/git"

inherit cmake

EXTRA_OECMAKE = "\
    -DPHYSFS_BUILD_SHARED=ON -DPHYSFS_BUILD_STATIC=OFF \
    -DPHYSFS_BUILD_TEST=OFF -DPHYSFS_BUILD_DOCS=OFF \
"
