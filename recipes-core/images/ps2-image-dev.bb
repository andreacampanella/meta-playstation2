SUMMARY = "PlayStation 2 USB drive image with a compiler, to build programs on the console"
DESCRIPTION = "ps2-image-usb with GCC and G++, binutils, make, autoconf, \
automake, libtool, pkg-config, CMake, git and gdb, and the headers of \
every library in the image (SDL2, OpenGL, ALSA and the others). The \
compiler on the console builds for the console ABI with no flags."
LICENSE = "MIT"

require ps2-image-usb.bb

# packagegroup-core-buildessential: gcc, g++, cpp, binutils, make, autoconf,
# automake, libtool, gettext, pkg-config. dev-pkgs: the -dev package (the
# headers and the .so links) of every package in the image.
# ps2-fastdf-staticdev: libfastdf.a, for "-Wl,--whole-archive -l:libfastdf.a
# -Wl,--no-whole-archive", the fast doubles of the games.
IMAGE_FEATURES += "dev-pkgs"
IMAGE_INSTALL += "packagegroup-core-buildessential cmake git gdb ps2-dev-env ps2-fastdf-staticdev"

# The compiler and the headers take about 300 MiB. Room for the sources
# and the build of a program on top of the 768 MiB for game data.
IMAGE_ROOTFS_EXTRA_SPACE = "1310720"
