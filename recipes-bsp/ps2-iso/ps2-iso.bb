SUMMARY = "PlayStation 2 disc image that contains the Linux kernel"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit deploy nopackages

PACKAGE_ARCH = "${MACHINE_ARCH}"
COMPATIBLE_MACHINE = "ps2"

DEPENDS = "cdrtools-native"
do_deploy[depends] += "virtual/kernel:do_deploy"

# The ELF file that the console starts from the disc. The default is the kernel
# with the initramfs. Set this to a loader such as uLaunchELF if the console
# does not start the kernel directly.
PS2_BOOT_ELF ?= "${DEPLOY_DIR_IMAGE}/${KERNEL_IMAGETYPE}-initramfs-${MACHINE}.bin"
# Name of the ELF file on the disc. Use an 8.3 name in capital letters.
PS2_BOOT_NAME ?= "VMLINUZ.ELF"
# "PAL" or "NTSC"
PS2_VMODE ?= "PAL"
# "cd" writes ISO 9660 only. "dvd" also writes UDF, which a DVD needs.
PS2_ISO_MEDIA ?= "cd"
PS2_ISO_VOLUME ?= "PS2LINUX"
# Optional directory. The recipe copies its contents to the root of the disc.
PS2_ISO_EXTRA_DIR ?= ""

do_configure[noexec] = "1"
do_compile[noexec] = "1"
do_install[noexec] = "1"

do_deploy() {
    isodir=${WORKDIR}/iso
    rm -rf $isodir
    install -d $isodir

    install -m 0644 ${PS2_BOOT_ELF} $isodir/${PS2_BOOT_NAME}
    # Keep the kernel on the disc when a loader is the boot file.
    kernel=${DEPLOY_DIR_IMAGE}/${KERNEL_IMAGETYPE}-initramfs-${MACHINE}.bin
    if [ "${PS2_BOOT_ELF}" != "$kernel" ]; then
        install -m 0644 $kernel $isodir/VMLINUZ.ELF
    fi

    if [ -n "${PS2_ISO_EXTRA_DIR}" ]; then
        cp -r ${PS2_ISO_EXTRA_DIR}/. $isodir/
    fi

    # The console reads SYSTEM.CNF to find the boot file. Lines end in CR LF.
    printf 'BOOT2 = cdrom0:\\%s;1\r\nVER = 1.00\r\nVMODE = %s\r\n' \
        "${PS2_BOOT_NAME}" "${PS2_VMODE}" > $isodir/SYSTEM.CNF

    udf=""
    if [ "${PS2_ISO_MEDIA}" = "dvd" ]; then
        udf="-udf"
    fi

    mkisofs -iso-level 1 $udf \
        -sysid PLAYSTATION -A PLAYSTATION -V "${PS2_ISO_VOLUME}" \
        -o ${DEPLOYDIR}/ps2-linux-${MACHINE}.iso $isodir
}
addtask deploy after do_prepare_recipe_sysroot before do_build
