# Fast doubles. A recipe that inherits this class links libfastdf.a
# (recipes-support/ps2-fastdf): the libgcc software double routines
# replaced by single precision operations on the FPU, 10 to 20 times
# faster, 24 bits of precision instead of 53. For game code that uses
# double by habit, as the Polymost renderer of Duke Nukem 3D does, where
# a profile showed half of the time in those routines.
#
# --whole-archive makes the linker take every routine, wherever the
# archive stands on the link line, so the definitions are in place before
# libgcc is searched. The shared libraries keep their exact doubles.
#
# Not for a program that needs the precision: a clock in seconds since
# 1970, coordinates above 16 million with a fraction.
DEPENDS += "ps2-fastdf"

# A constant such as 2.0 is a float, not a double: f * 2.0 is then one
# FPU instruction instead of two conversions and a routine of this
# library. A program that links fastdf has float precision in its
# doubles already, so the constants lose nothing it still had.
CFLAGS:append = " -fsingle-precision-constant"
CXXFLAGS:append = " -fsingle-precision-constant"
LDFLAGS:append = " -Wl,--whole-archive -l:libfastdf.a -Wl,--no-whole-archive"
