SUMMARY = "pfsshell: the APA and PFS formats of the PlayStation 2 hard disk"
DESCRIPTION = "A shell for the hard disk of the PlayStation 2, built from \
the APA and PFS drivers of ps2sdk. It formats a disk as APA, makes and \
removes partitions, and copies files into and out of PFS partitions. \
ps2-disk uses it to make the Linux partitions and to put the kernel into \
the PFS partition +linux, where uLaunchELF starts it."
HOMEPAGE = "https://github.com/ps2homebrew/pfsshell"
SECTION = "console/utils"

# pfsshell is GPL-2.0. The APA, PFS and iomanX code comes from ps2sdk,
# which is AFL-2.0.
LICENSE = "GPL-2.0-only & AFL-2.0"
LIC_FILES_CHKSUM = "file://COPYING;md5=393a5ca445f6965873eca0259a17f833 \
                    file://COPYING.AFLv2;md5=9eac3bbab2538c7f0efa6ce8ea8c1078"

# The ps2sdk submodule, at the commit that pfsshell pins. The symbolic
# links in subprojects/ point into external/ps2sdk. The other submodules
# (dokany for Windows, meson toolchain files) are not needed.
SRC_URI = "git://github.com/ps2homebrew/pfsshell.git;protocol=https;branch=master;name=pfsshell \
           git://github.com/ps2dev/ps2sdk.git;protocol=https;branch=master;name=ps2sdk;destsuffix=git/external/ps2sdk \
          "
SRCREV_pfsshell = "8c92467b3d715c3698f1f8ce63a8a07e214d6c73"
SRCREV_ps2sdk = "023b678f3548be2fb0af7d10a65c6662e53f59cf"
SRCREV_FORMAT = "pfsshell_ps2sdk"

S = "${WORKDIR}/git"

inherit meson pkgconfig

# pfs2tar (a PFS partition to a tar file) is useful for a backup. pfsfuse,
# pfsd and ps2kinst need FUSE or SysV IPC and are not built.
EXTRA_OEMESON = "-Denable_pfsfuse=false -Denable_pfsd=false -Denable_ps2kinst=false -Denable_pfs2tar=true"

# The build also installs libps2hdd.so, the same code as a library with no
# version. Nothing on the console links with it.
FILES_SOLIBSDEV = ""
FILES:${PN} += "${libdir}/libps2hdd.so"
INSANE_SKIP:${PN} += "dev-so"
