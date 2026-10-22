SUMMARY = "Small BusyBox root filesystem for the PlayStation 2 kernel"
LICENSE = "MIT"

# ps2-initramfs-init supplies /init. It loads the kernel modules in
# PS2_INITRAMFS_MODULES (see conf/machine/ps2.conf). The package manager adds
# the modules that these modules need (SIF, IOP, HID).
# ps2-iopmod supplies the IOP firmware that iop-irq loads (irqrelay.irx).
PACKAGE_INSTALL = "busybox busybox-inittab base-files base-passwd \
    ps2-initramfs-init ps2-iopmod ps2-libgl ps2-doom \
    ${@' '.join('kernel-module-' + m for m in (d.getVar('PS2_INITRAMFS_MODULES') + ' ' + d.getVar('PS2_INITRAMFS_EXTRA_MODULES')).split())}"

# Log in as root with no password on tty1.
IMAGE_FEATURES = "empty-root-password allow-empty-password allow-root-login"
IMAGE_LINGUAS = ""
NO_RECOMMENDATIONS = "1"

IMAGE_FSTYPES = "${INITRAMFS_FSTYPES}"
# kernel.bbclass looks for <image>-<machine>.cpio.gz, without ".rootfs".
IMAGE_NAME_SUFFIX = ""
IMAGE_ROOTFS_SIZE = "0"
IMAGE_ROOTFS_EXTRA_SPACE = "0"
IMAGE_OVERHEAD_FACTOR = "1.0"

# Make static device nodes. The kernel opens /dev/console before init starts.
USE_DEVFS = "0"

inherit core-image
