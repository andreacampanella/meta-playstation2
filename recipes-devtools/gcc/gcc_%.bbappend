# The GCC that runs on the console: its defaults match the tune of the
# image, so "gcc hello.c" on the console makes a program with the ABI of
# the system libraries, with no flags.
#
# The cross compiler gets the tune flags from the build (TUNE_CCARGS) and
# needs no defaults. This file changes only the recipe "gcc", the compiler
# in the image.
#
#   r5900el-fpu  -march=r5900 (which turns on -mfix-r5900) and the single
#                precision FPU: --with-fpu=single
#   r5900el-nf   -march=mips3, soft float (Poky sets --with-float=soft)
#   r5900el      -march=mips3, hard float with doubles
# For the two mips3 tunes, the specs add -mfix-r5900, the workaround of the
# short loop erratum, as TUNE_CCARGS does.
EXTRA_OECONF:append:ps2 = "${@bb.utils.contains('TUNE_FEATURES', 'r5900fpu', ' --with-arch=r5900 --with-fpu=single', '', d)}"
EXTRA_OECONF:append:ps2 = "${@bb.utils.contains('TUNE_FEATURES', 'r5900', ' --with-arch=mips3 --with-specs=%{!mno-fix-r5900:-mfix-r5900}', '', d)}"
