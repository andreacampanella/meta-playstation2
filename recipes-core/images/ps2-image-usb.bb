SUMMARY = "PlayStation 2 image for a USB drive"
DESCRIPTION = "A USB drive image with three partitions: a FAT partition with \
the kernel for uLaunchELF, an ext4 root file system and a swap partition. \
The root file system is on the drive, so the RAM stays free for programs."
LICENSE = "MIT"

# Add packages with CORE_IMAGE_EXTRA_INSTALL in conf/local.conf.
IMAGE_INSTALL = "packagegroup-core-boot kernel-modules ps2-iopmod ps2-libgl ps2-demo ps2-doom freedoom ps2-games-folders jfduke3d jfduke3d-polymost quakespasm dxx-rebirth ps2-net ps2-zram ps2-disk ps2-sound alsa-utils-aplay alsa-utils-amixer alsa-utils-speakertest strace ${CORE_IMAGE_EXTRA_INSTALL}"

# Log in as root with no password on tty1.
IMAGE_FEATURES += "empty-root-password allow-empty-password allow-root-login"
IMAGE_LINGUAS = ""

IMAGE_FSTYPES = "wic.gz"
WKS_FILE = "ps2-usb.wks.in"

# The FAT partition gets the kernel with the initramfs as VMLINUZ.ELF.
IMAGE_BOOT_FILES = "${KERNEL_IMAGETYPE}-initramfs-${MACHINE}.bin;VMLINUZ.ELF"
do_image_wic[depends] += "virtual/kernel:do_deploy"

# Size of the FAT partition. The kernel takes 8 MiB. The games still look
# on it after ~/games and /data, so a PC can copy game data there.
PS2_BOOT_PARTITION_SIZE ?= "256"
WICVARS:append = " PS2_BOOT_PARTITION_SIZE"

# Free space in the root file system, in KiB: 768 MiB, for the game data
# in ~/games. Duke Nukem 3D takes 25 MiB (full game) or 11 MiB
# (shareware), Doom 4 to 12 MiB, Quake 18 MiB (shareware) or 180 MiB (the
# 2021 release), Descent about 10 MiB.
IMAGE_ROOTFS_EXTRA_SPACE = "786432"

inherit core-image
