# The single-float tune (r5900el-fpu): see conf/machine/include/tune-r5900.inc.
# The patch only changes a build with -march=r5900.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://0001-libgcc-No-MIPS16-float-stubs-for-the-R5900.patch \
            file://0002-libstdc-from_chars-for-long-double-without-fast_float.patch \
            file://0003-libgcc-Software-double-for-a-single-float-MIPS.patch"
