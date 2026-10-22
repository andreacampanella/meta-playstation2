# The single-float tune (r5900el-fpu): see conf/machine/include/tune-r5900.inc.
# The patch only changes a build with -msingle-float.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://0001-mips-No-sqrt-builtin-with-single-float.patch \
            file://0002-No-_Float32-for-the-R5900.patch \
            file://0003-mips-setjmp-with-32-bit-FPU-stores-on-the-R5900.patch \
            file://0004-mips-ucontext-with-32-bit-FPU-stores-on-the-R5900.patch"
