# The single-float tune (r5900el-fpu): see conf/machine/include/tune-r5900.inc.
# The patch only changes a build with -msingle-float.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://0001-mips-The-o32-single-float-ABI.patch"
