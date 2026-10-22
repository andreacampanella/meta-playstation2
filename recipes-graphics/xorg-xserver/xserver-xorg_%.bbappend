# The X server for the PlayStation 2: the fbdev driver on /dev/fb0 (kernel
# patch 0017) and the CPU draws. No GLX, glamor or DRI: they need Mesa,
# whose libGL would collide with ps2-libgl, and the GS has no DRM driver.
# No udev: the init is mdev, and ps2-x writes the input devices into the
# configuration. Input through evdev, not libinput, which needs udev.
PACKAGECONFIG:remove:ps2 = "dri dri2 dri3 glx glamor udev systemd-logind"
XSERVER_RDEPENDS:ps2 = "xkeyboard-config rgb xserver-xf86-config xkbcomp xf86-input-evdev"

# udev_kms is a second option, on by default, that needs libudev too
# ("Dependency libudev not found" with udev=false).
EXTRA_OEMESON:append:ps2 = " -Dudev_kms=false"
