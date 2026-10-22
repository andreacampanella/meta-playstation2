SUMMARY = "SDL2_image: image loading for SDL2 (PCX and BMP only here)"
DESCRIPTION = "DXX-Rebirth (Descent) reads its PCX pictures through \
SDL2_image. This build has the formats that need no other library: PCX, \
BMP, TGA, PNM, XPM, LBM, GIF, QOI. No PNG, JPEG, WebP, AVIF or TIFF."
HOMEPAGE = "https://github.com/libsdl-org/SDL_image"
LICENSE = "Zlib"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=2f6d9e01c97958aa851954ed5acf82ee"

SRC_URI = "git://github.com/libsdl-org/SDL_image.git;protocol=https;nobranch=1"
# Tag release-2.8.2
SRCREV = "abcf63aa71b4e3ac32120fa9870a6500ddcdcc89"

S = "${WORKDIR}/git"

DEPENDS = "libsdl2"

inherit cmake pkgconfig

EXTRA_OECMAKE = "\
    -DSDL2IMAGE_VENDORED=OFF -DSDL2IMAGE_SAMPLES=OFF -DSDL2IMAGE_TESTS=OFF \
    -DSDL2IMAGE_BACKEND_STB=OFF -DSDL2IMAGE_DEPS_SHARED=OFF \
    -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_JPG=OFF -DSDL2IMAGE_JXL=OFF \
    -DSDL2IMAGE_PNG=OFF -DSDL2IMAGE_SVG=OFF -DSDL2IMAGE_TIF=OFF \
    -DSDL2IMAGE_WEBP=OFF -DSDL2IMAGE_XCF=OFF -DSDL2IMAGE_XV=OFF \
    -DSDL2IMAGE_BMP=ON -DSDL2IMAGE_PCX=ON -DSDL2IMAGE_TGA=ON \
    -DSDL2IMAGE_PNM=ON -DSDL2IMAGE_XPM=ON -DSDL2IMAGE_LBM=ON \
    -DSDL2IMAGE_GIF=ON -DSDL2IMAGE_QOI=ON \
"

# The build installs a copy of the license, which no package wants.
do_install:append() {
    rm -rf ${D}${datadir}/licenses
    rmdir ${D}${datadir} 2>/dev/null || true
}
