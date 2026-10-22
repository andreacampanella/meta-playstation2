# nc with a listen mode (nc -l -p PORT): a plain file copy to the console,
# with no SSH encryption on its slow CPU.
#
# ps2-disk.cfg: blockdev, so ps2-disk can make the kernel read the new
# partition table after pfsshell writes it (blockdev --rereadpt). Swap
# labels in blkid and in "swapon LABEL=", so /init and ps2-disk find a
# swap partition by its label.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append:ps2 = " file://nc-server.cfg file://ps2-disk.cfg"
