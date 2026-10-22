# GTK+ 3 for the matchbox panel: the X11 back end only.
#
# Poky turns on opengl and wayland in DISTRO_FEATURES, and GTK+ would then
# pull libepoxy and Mesa for an EGL and a GL that the console does not have.
PACKAGECONFIG:ps2 = "x11"
