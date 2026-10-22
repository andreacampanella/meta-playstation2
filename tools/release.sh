#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Gather a release: run it on the PC after
#
#   kas build meta-playstation2/kas/ps2.yml:meta-playstation2/kas/release.yml
#
# usage: meta-playstation2/tools/release.sh <build dir> <output dir>
#
# The output directory gets, for the version in conf/layer.conf:
#
#   ps2-image-usb-<version>.wic.gz   the USB drive image
#   ps2-linux-<version>.iso          the disc image
#   vmlinuz-initramfs-<version>.bin  the kernel with a root file system in RAM
#   license.manifest                 every package of the image and its license
#   sources-<version>.tar            the GPL, LGPL and AGPL sources with patches
#   meta-playstation2-<version>.tar.gz        this layer, with its kas files
#   SHA256SUMS
set -e
BUILD=${1:?usage: release.sh <build dir> <output dir>}
OUT=${2:?usage: release.sh <build dir> <output dir>}
LAYER=$(cd "$(dirname "$0")/.." && pwd)
DEPLOY=$BUILD/tmp/deploy
IMAGES=$DEPLOY/images/ps2

VERSION=$(sed -n 's/^PS2_LAYER_VERSION = "\([^ "]*\).*/\1/p' "$LAYER/conf/layer.conf")
[ -n "$VERSION" ] || { echo "no PS2_LAYER_VERSION in $LAYER/conf/layer.conf" >&2; exit 1; }
[ -d "$DEPLOY/sources" ] || { echo "no $DEPLOY/sources: build with kas/release.yml" >&2; exit 1; }

mkdir -p "$OUT"
cp -L "$IMAGES/ps2-image-usb-ps2.rootfs.wic.gz" "$OUT/ps2-image-usb-$VERSION.wic.gz"
cp -L "$IMAGES/ps2-linux-ps2.iso" "$OUT/ps2-linux-$VERSION.iso"
cp -L "$IMAGES/vmlinuz-initramfs-ps2.bin" "$OUT/vmlinuz-initramfs-$VERSION.bin"

# The manifest of the newest USB image
MANIFEST=$(ls -t "$DEPLOY"/licenses/*/ps2-image-usb-ps2*/license.manifest 2>/dev/null | head -1)
[ -n "$MANIFEST" ] || { echo "no license.manifest for ps2-image-usb under $DEPLOY/licenses" >&2; exit 1; }
cp "$MANIFEST" "$OUT/license.manifest"

# The files in sources/ are compressed already
tar -cf "$OUT/sources-$VERSION.tar" -C "$DEPLOY" sources

# The layer, without a build or a git directory in it
tar -czf "$OUT/meta-playstation2-$VERSION.tar.gz" -C "$(dirname "$LAYER")" \
    --exclude=.git --exclude=build "$(basename "$LAYER")"


(cd "$OUT" && sha256sum ps2-image-usb-$VERSION.wic.gz ps2-linux-$VERSION.iso \
    vmlinuz-initramfs-$VERSION.bin license.manifest sources-$VERSION.tar \
    meta-playstation2-$VERSION.tar.gz > SHA256SUMS)
ls -l "$OUT"
