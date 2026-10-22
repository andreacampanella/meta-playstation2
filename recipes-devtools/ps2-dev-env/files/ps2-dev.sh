# Settings for building programs on the console (ps2-image-dev).
#
# /tmp is in RAM (tmpfs), and the console has 32 MiB. GCC writes its
# temporary files to TMPDIR, so they go to the disk instead.
TMPDIR="$HOME/.cache/tmp"
mkdir -p "$TMPDIR" 2>/dev/null
export TMPDIR
# One job at a time: two compilers at once do not fit in the RAM.
MAKEFLAGS="-j1"
export MAKEFLAGS
