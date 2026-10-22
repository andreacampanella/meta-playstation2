# Profiling with gprof. A recipe that inherits this class builds for gprof
# when local.conf has
#
#     PS2_PROFILE = "1"
#
# -pg goes into the compiler and the linker: each function counts its
# calls, and the C library samples the program counter 100 times a second.
# The program writes gmon.out in its working directory when it exits
# normally (a kill writes nothing). The package keeps the symbols, so the
# binary in the work directory and the one on the console are the same
# file. tools/profile.sh fetches gmon.out and runs gprof.
#
# The samples cover the program, not the shared libraries, so in this
# mode the ps2-libgl recipe builds libGL.a and a libGL.so linker script
# that links it: a game built in this mode has the library inside, and
# the profile shows the library's functions (the HUD's gl and sw). The software double
# routines (__muldf3 and the others) come from libgcc_s.so.1 when the
# program links with g++, and would be invisible too, so the profiling
# build links libgcc statically.
PS2_PROFILE ?= "0"

CC:append = "${@' -pg' if d.getVar('PS2_PROFILE') == '1' else ''}"
CXX:append = "${@' -pg' if d.getVar('PS2_PROFILE') == '1' else ''}"
# --export-dynamic: the SDL2 driver finds the library inside the program
# (dlsym on the program's own handle) and uses that copy for the display,
# instead of loading libGL.so.1 as a second copy.
LDFLAGS:append = "${@' -static-libgcc -Wl,--export-dynamic' if d.getVar('PS2_PROFILE') == '1' else ''}"
INHIBIT_PACKAGE_STRIP = "${@'1' if d.getVar('PS2_PROFILE') == '1' else '0'}"
INHIBIT_PACKAGE_DEBUG_SPLIT = "${@'1' if d.getVar('PS2_PROFILE') == '1' else '0'}"
