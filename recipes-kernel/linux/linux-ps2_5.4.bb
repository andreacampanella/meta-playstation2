SUMMARY = "Linux kernel for the Sony PlayStation 2 (frno7 fork)"
SECTION = "kernel"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=bbea815ee2795b2f4230826c0c6b8814"

inherit kernel

SRC_URI = "git://github.com/frno7/linux.git;protocol=https;branch=ps2-main"
# Head of ps2-main on 2026-10-12.
SRCREV = "77174dd4c8958a91aa5b76fb064a60eb68119e70"

# Patches 1 to 5: Ethernet driver for the network adapter (SMAP), from pull
# request 96 of the fork. The first two also give power to the expansion bay,
# which the hard disk driver needs.
# Patch 6: the device /dev/ps2gs, which sends GS packets with DMA.
# Patch 7: the hard disk driver resets the ATA bus before it looks for a
# disk, as the system software does. A SATA bridge needs that.
# Patch 8: the hard disk driver uses PIO unless loaded with dma=1. Its DMA
# path gives a bus error with a fast disk.
# Patch 9: the hard disk driver polls the PIO commands (the interrupt line
# of the adapter is not reliable) and offers no TRIM without DMA.
# Patch 10: /dev/ps2gs keeps a 256 KiB packet buffer from the start, so a
# program gets one after hours of swapping too.
# Patch 11: the R5900 FPU (single precision) instead of soft float.
# Patch 12: /dev/ps2gs receives a local-to-host transfer (glReadPixels).
# Patch 13: /dev/ps2gs maps the packet buffer uncached accelerated.
# Patch 14: one SIF1 sender at a time, with a claim that spins with
# interrupts on. The sound driver sends from a tasklet.
# Patch 15: the kernel option SND_PS2, which selects the ALSA PCM core for
# the sound driver (recipes-kernel/ps2-sound).
# Patch 16: the ROMs out of the memory map. The page array covered the
# addresses up to ROM0 at 512 MiB: 4 MiB of RAM for 8192 pages.
# Patch 17: /dev/fb0 can be mapped: the pixels in RAM, sent to the GS
# with deferred I/O. For an X server with the fbdev driver.
# Patch 18: the APA partition format of the hard disk, the format of the
# console software, uLaunchELF and OPL. Each APA partition is a Linux
# partition named after it (PARTNAME), without its first 4 MiB.
SRC_URI += "\
    file://0001-MIPS-PS2-IOP-Fix-iop_write-b-w-l-issuing-the-LOADFIL.patch \
    file://0002-MIPS-PS2-IOP-Actually-initialise-the-DEV9-expansion-.patch \
    file://0003-net-ps2-Add-PlayStation-2-SMAP-Ethernet-driver.patch \
    file://0004-MIPS-PS2-Register-the-SMAP-Ethernet-device.patch \
    file://0005-net-ps2-Close-the-interface-on-machine-shutdown.patch \
    file://0006-MIPS-PS2-Add-the-Graphics-Synthesizer-device-dev-ps2.patch \
    file://0007-ata-pata_ps2-Reset-the-ATA-bus-before-the-probe.patch \
    file://0008-ata-pata_ps2-PIO-only-unless-dma-1.patch \
    file://0009-ata-pata_ps2-Poll-PIO-commands-and-offer-no-TRIM.patch \
    file://0010-ps2gs-Keep-a-256-KiB-packet-buffer-from-the-start.patch \
    file://0011-MIPS-R5900-Use-the-FPU.patch \
    file://0012-ps2gs-Receive-a-local-to-host-transfer.patch \
    file://0013-ps2gs-Map-the-packet-buffer-uncached-accelerated.patch \
    file://0014-ps2-sif-One-sender-at-a-time-on-SIF1.patch \
    file://0015-ALSA-mips-Add-the-option-SND_PS2-for-the-PlayStation-2.patch \
    file://0016-MIPS-PS2-Keep-the-ROMs-out-of-the-memory-map.patch \
    file://0017-fbdev-ps2fb-Mapped-pixels-in-RAM-sent-to-the-GS-with.patch \
    file://0018-block-partitions-Read-the-APA-format-of-the-PlayStat.patch \
"

LINUX_VERSION = "5.4.221"
PV = "${LINUX_VERSION}+git"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "ps2"

# Two generated source files contain build paths. They go only in the -src package.
INSANE_SKIP:${PN}-src += "buildpaths"

# Set PS2_CMDLINE to replace the kernel command line. The frame buffer driver
# reads "video=ps2fb:<mode>", for example
# "video=ps2fb:640x480@60 console=tty0 console=tty1".
# With no mode, the driver uses 576x460i@50 (PAL) or 576x384i@60 (NTSC).
PS2_CMDLINE ?= ""

# ps2_defconfig builds these drivers as modules. The recipe puts them in the
# kernel. The USB drivers must stay modules, because the IOP and SIF drivers
# that they use are always modules in this fork. PS2_APA_PARTITION is the
# APA reader of patch 18. Its default is already y on the PS2, and the
# list names it so that the choice is visible here.
PS2_BUILTIN_DRIVERS ?= "FB_PS2 PS2_APA_PARTITION"

# Drivers for USB network adapters, as modules. ps2_defconfig has only the
# AX88179 driver. The console has USB 1.1, so each adapter runs at 12 Mbit/s.
PS2_MODULE_DRIVERS ?= "USB_NET_AX8817X USB_RTL8150 USB_RTL8152 USB_PEGASUS \
    USB_NET_CDCETHER USB_NET_RNDIS_HOST USB_NET_DM9601 USB_NET_SMSC95XX \
    USB_NET_MCS7830 USB_NET_SR9700"

# Kernel options turned off for RAM. The image in RAM is 4.3 MiB with this
# list and 5.4 MiB without it (code, data and tables, measured with nm).
#
# Debug: the symbol table (315 KiB, see below), kernel probes, debugfs,
# allocator debug, SCSI message tables, the kernel config in /proc, /proc/kcore,
# /dev/kmem, relay.
# Unused parts: the BPF system call (190 KiB), performance events (70 KiB),
# io_uring, the key service, 9P (64 KiB), the cipher set that nothing in this
# kernel uses (AES, GCM, CCM, RSA, ARC4, SHA-256, the random generator; 160
# KiB; ext4 keeps its CRC32C), wireless, squashfs, ext2 (ext4 mounts it),
# the RAM disk, block integrity and bsg, the legacy ptys, the BCJ filters
# of other architectures in the xz decoder, two empty network vendor menus,
# module version tables.
#
# With KALLSYMS off, a kernel oops prints addresses and no function names.
# To resolve one, look up the address in System.map in the deploy directory,
# or build with symbols: PS2_KERNEL_OFF:remove = "KALLSYMS" in local.conf.
PS2_KERNEL_OFF ?= "KALLSYMS KALLSYMS_ALL KPROBES DEBUG_FS BLK_DEBUG_FS \
    SLUB_DEBUG DEBUG_MEMORY_INIT SCSI_LOGGING SCSI_CONSTANTS ATA_VERBOSE_ERROR \
    IKCONFIG PROC_KCORE DEVKMEM RELAY \
    BPF_SYSCALL PERF_EVENTS IO_URING KEYS NET_9P 9P_FS \
    CRYPTO_AES CRYPTO_GCM CRYPTO_CCM CRYPTO_RSA CRYPTO_ARC4 CRYPTO_LIB_ARC4 \
    CRYPTO_SHA256 CRYPTO_HMAC CRYPTO_CBC CRYPTO_CTR CRYPTO_ECB CRYPTO_NULL \
    CRYPTO_SEQIV CRYPTO_ECHAINIV CRYPTO_DRBG_MENU CRYPTO_JITTERENTROPY \
    CRYPTO_TEST \
    WIRELESS CFG80211 SQUASHFS EXT2_FS BLK_DEV_RAM BLK_DEV_INTEGRITY \
    BLK_DEV_BSG LEGACY_PTYS NET_VENDOR_CADENCE NET_VENDOR_MELLANOX \
    XZ_DEC_X86 XZ_DEC_POWERPC XZ_DEC_IA64 XZ_DEC_ARM XZ_DEC_ARMTHUMB \
    XZ_DEC_SPARC MODVERSIONS NAMESPACES DEBUG_INFO"

# zram: a compressed swap device in RAM, far faster than the USB drive.
# Sound: the ALSA core as modules (about 120 KiB), loaded with snd-ps2.
# ATA: the ATA core as a module (130 KiB), loaded with pata-ps2 by ps2-disk.
PS2_KERNEL_MODULES ?= "ZRAM ZSMALLOC CRYPTO_LZO LZO_COMPRESS LZO_DECOMPRESS \
    SOUND SND SND_PS2 ATA"

# Kernel log buffer: 16 KiB instead of 64 KiB.
PS2_LOG_BUF_SHIFT ?= "14"

# Module packages must not pull the kernel image into the initramfs.
RDEPENDS:${KERNEL_PACKAGE_NAME}-base = ""
RRECOMMENDS:${KERNEL_PACKAGE_NAME}-base = ""

# Address of the compressed kernel in memory. The kernel unpacks itself to
# 0x80010000 (physical 0x00010000) and must end below this address. The fork
# uses 0x00800000, which leaves less than 8 MiB. A kernel with a glibc
# initramfs is larger, and the unpacked data then overwrites the unpack code.
PS2_ZLOAD_ADDRESS ?= "0x01000000"

do_configure:prepend() {
    sed -i -e 's|^\(zload-.*+=\).*|\1 ${PS2_ZLOAD_ADDRESS}|' ${S}/arch/mips/ps2/Platform

    oe_runmake -C ${S} O=${B} ps2_defconfig

    # ps2_defconfig points to "../initramfs", which does not exist here.
    # kernel.bbclass supplies the initramfs when it bundles the image.
    sed -i -e 's|^CONFIG_INITRAMFS_SOURCE=.*|CONFIG_INITRAMFS_SOURCE=""|' ${B}/.config

    # Without ps2fb in the kernel, the console has no display.
    for sym in ${PS2_BUILTIN_DRIVERS}; do
        sed -i -e "/^CONFIG_$sym=/d" -e "/^# CONFIG_$sym is not set/d" ${B}/.config
        echo "CONFIG_$sym=y" >> ${B}/.config
    done

    for sym in ${PS2_MODULE_DRIVERS}; do
        sed -i -e "/^CONFIG_$sym=/d" -e "/^# CONFIG_$sym is not set/d" ${B}/.config
        echo "CONFIG_$sym=m" >> ${B}/.config
    done

    # Smaller kernel in RAM: no debug tables, no drivers the console has no
    # use for. The console has 32 MiB and the kernel keeps its share for
    # the whole run.
    #
    # EXPERT shows the options that Kconfig hides otherwise (KALLSYMS,
    # IO_URING, SLUB_DEBUG). A hidden option goes back to its default
    # when "make olddefconfig" runs, whatever .config says.
    sed -i -e '/^CONFIG_EXPERT=/d' -e '/^# CONFIG_EXPERT is not set/d' ${B}/.config
    echo "CONFIG_EXPERT=y" >> ${B}/.config
    for sym in ${PS2_KERNEL_OFF}; do
        sed -i -e "/^CONFIG_$sym=/d" -e "/^# CONFIG_$sym is not set/d" ${B}/.config
        echo "# CONFIG_$sym is not set" >> ${B}/.config
    done
    for sym in ${PS2_KERNEL_MODULES}; do
        sed -i -e "/^CONFIG_$sym=/d" -e "/^# CONFIG_$sym is not set/d" ${B}/.config
        echo "CONFIG_$sym=m" >> ${B}/.config
    done
    sed -i -e 's|^CONFIG_LOG_BUF_SHIFT=.*|CONFIG_LOG_BUF_SHIFT=${PS2_LOG_BUF_SHIFT}|' ${B}/.config

    if [ -n "${PS2_CMDLINE}" ]; then
        sed -i -e 's|^CONFIG_CMDLINE=.*|CONFIG_CMDLINE="${PS2_CMDLINE}"|' ${B}/.config
    fi
}

# Warn if the unpacked kernel does not fit below PS2_ZLOAD_ADDRESS.
do_deploy:append() {
    end=$(${HOST_PREFIX}nm ${B}/vmlinux | awk '$3 == "_end" { print $1 }')
    if [ -n "$end" ]; then
        end_phys=$(printf '%d' "0x${end#80}")
        limit=$(printf '%d' "${PS2_ZLOAD_ADDRESS}")
        bbnote "Unpacked kernel ends at physical address $end_phys. The limit is $limit."
        if [ "$end_phys" -ge "$limit" ]; then
            bbwarn "The unpacked kernel ends at $end_phys, which is not below PS2_ZLOAD_ADDRESS ($limit). The kernel will not start."
        fi
    fi
}

# "make vmlinuz" writes the file to the top of the build directory.
# kernel.bbclass looks for the file in arch/mips/boot.
do_kernel_link_images:append() {
    ln -sf ../../../vmlinuz ${B}/arch/${ARCH}/boot/vmlinuz
}
