# SDL2 for Linux on the PlayStation 2.
#
# The patch adds the "ps2linux" video driver. A program that uses SDL2 then
# shows its picture on the console and gets the USB keyboard and mouse, with
# no change to the program.
#
# The console has no X11, Wayland or Mesa, so the optional parts are off,
# except ALSA: the sound driver snd-ps2 (recipes-kernel/ps2-sound) gives
# one playback device. The ps2linux driver gives OpenGL windows through
# libGL.so.1 on its own, and the patch makes SDL_config.h say
# SDL_VIDEO_OPENGL, which programs such as DXX-Rebirth test at build
# time. Without the dummy and
# offscreen drivers, a program stops with an error message if the ps2linux
# driver cannot start.

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:ps2 = " file://0001-video-add-the-ps2linux-driver.patch"

PACKAGECONFIG:ps2 = "alsa"

# No "dummy" or "disk" audio driver. With one of them, SDL2 falls back to
# it when ALSA fails, and a game then waits forever for a sound that never
# ends (Duke Nukem 3D at the level start). Without them,
# SDL_Init(SDL_INIT_AUDIO) fails, and a game uses its own "no sound" path.
EXTRA_OECMAKE:append:ps2 = " -DSDL_PS2LINUX=ON -DSDL_OFFSCREEN=OFF -DSDL_DUMMYAUDIO=OFF -DSDL_DISKAUDIO=OFF"
