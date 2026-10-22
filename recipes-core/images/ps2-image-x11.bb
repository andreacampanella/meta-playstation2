SUMMARY = "PlayStation 2 image for a USB drive, with the X desktop"
DESCRIPTION = "ps2-image-usb with X, the matchbox window manager and a \
terminal. ps2-x on the console starts the desktop. The console image \
without X stays ps2-image-usb."
LICENSE = "MIT"

require ps2-image-usb.bb

IMAGE_INSTALL:append = " ps2-x11-session xeyes xev xset"

REQUIRED_DISTRO_FEATURES = "x11"
inherit features_check
