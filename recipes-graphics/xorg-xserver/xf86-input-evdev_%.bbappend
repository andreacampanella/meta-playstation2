# The evdev driver needs libudev at build time. libudev is the library of
# eudev, which the build has for alsa-utils already. The image gets the
# library alone (libudev1), no udev daemon: the init stays mdev, and the
# library reads /sys directly when no udev database exists.
DEPENDS:append:ps2 = " udev"
