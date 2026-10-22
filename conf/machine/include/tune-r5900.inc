# Tunes for the Sony/Toshiba R5900 (Emotion Engine), little endian, o32 ABI.
#
# -mfix-r5900 applies the workaround for the short loop erratum. The two
# tunes with doubles in their ABI use -march=mips3, because -march=r5900
# selects single-precision float. Three tunes are available:
#
#   r5900el     Hard-float ABI with double precision (-march=mips3). The
#               console FPU has no double format, so this tune needs the
#               kernel's FPU emulation: "nofpu" on the kernel command line.
#               One operation then takes about 10000 ns.
#   r5900el-nf  Soft-float ABI. The compiler calls integer routines for
#               floating point math. Much faster than the emulation.
#   r5900el-fpu Hard single-float ABI (-march=r5900 -msingle-float). float
#               math runs on the FPU, double math in software (libgcc). The
#               kernel enables the FPU (patch 0011). The FPU does not follow
#               IEEE 754: no NaN, no infinity, no denormals, and it rounds
#               toward zero. Fine for games, not for numerical code.
#
# Set DEFAULTTUNE in conf/local.conf. A change builds all packages again.
# A package built with one tune cannot link with another: the three are
# three ABIs.

DEFAULTTUNE ?= "r5900el-nf"

require conf/machine/include/mips/arch-mips.inc

TUNEVALID[r5900] = "Build for the R5900 CPU and apply its errata workarounds"
TUNE_CCARGS .= "${@bb.utils.contains('TUNE_FEATURES', 'r5900', ' -march=mips3 -mfix-r5900', '', d)}"

TUNEVALID[r5900fpu] = "Build for the R5900 CPU and its single precision FPU"
TUNE_CCARGS .= "${@bb.utils.contains('TUNE_FEATURES', 'r5900fpu', ' -march=r5900 -mfix-r5900 -msingle-float', '', d)}"

AVAILTUNES += "r5900el r5900el-nf r5900el-fpu"

TUNE_FEATURES:tune-r5900el = "o32 fpu-hard r5900"
MIPSPKGSFX_VARIANT:tune-r5900el = "r5900el"
PACKAGE_EXTRA_ARCHS:tune-r5900el = "mipsel r5900el"

TUNE_FEATURES:tune-r5900el-nf = "o32 r5900"
MIPSPKGSFX_VARIANT:tune-r5900el-nf = "r5900el"
PACKAGE_EXTRA_ARCHS:tune-r5900el-nf = "mipsel-nf r5900el-nf"

TUNE_FEATURES:tune-r5900el-fpu = "o32 fpu-hard r5900fpu"
MIPSPKGSFX_VARIANT:tune-r5900el-fpu = "r5900elfpu"
PACKAGE_EXTRA_ARCHS:tune-r5900el-fpu = "r5900elfpu"
