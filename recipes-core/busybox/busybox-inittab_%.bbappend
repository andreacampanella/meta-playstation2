# Start the zram swap, the hard disk, the sound driver and the network
# script, if the image has them. The initramfs uses the same inittab and
# does not have the scripts. Nothing in this init reads modules-load.d, so
# the sound driver loads from here, after the root switch, when the IOP
# firmware in /lib/firmware is there.
#
# run-postinsts does the package steps that the build could not do. The
# build cannot run R5900 programs (no qemu-usermode), so for example the
# font cache and the image loader cache wait in /etc/rpm-postinsts for
# the console. The script normally starts from /etc/rcS.d, which this
# init does not have. Each step deletes itself when it is done, so after
# the first boot the line costs almost nothing.
do_install:append:ps2() {
    echo "::sysinit:/bin/sh -c 'test -x /usr/sbin/ps2-zram && exec /usr/sbin/ps2-zram'" >> ${D}${sysconfdir}/inittab
    echo "::sysinit:/bin/sh -c 'test -x /usr/sbin/ps2-disk && exec /usr/sbin/ps2-disk mount'" >> ${D}${sysconfdir}/inittab
    echo "::sysinit:/bin/sh -c 'test -x /usr/sbin/run-postinsts && exec /usr/sbin/run-postinsts'" >> ${D}${sysconfdir}/inittab
    echo "::once:/bin/sh -c 'modprobe snd-ps2 2>/dev/null; true'" >> ${D}${sysconfdir}/inittab
    echo "::once:/bin/sh -c 'test -x /usr/sbin/ps2-net && exec /usr/sbin/ps2-net'" >> ${D}${sysconfdir}/inittab
}
