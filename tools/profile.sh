#!/bin/sh
# Fetch the gprof data of a program from the console and print the
# profile. Run it on the PC. The recipe must be built with
# PS2_PROFILE = "1" (classes/ps2-profile.bbclass), and the program must
# exit normally on the console, which writes gmon.out in its working
# directory.
#
# usage: tools/profile.sh <build dir> <recipe> [user@]host [gmon.out path] [binary]
#
#   gmon.out path  on the console, default /home/root/gmon.out
#   binary         the name of the program, when the package has several
#                  (default: the largest one)
#
# Prints the flat profile (the functions by time) and the call graph into
# profile-<recipe>.txt, and the first forty lines on the terminal. The
# soft-double routines show as __adddf3, __muldf3, __extendsfdf2 and so
# on: their callers are in the call graph.
set -e
BUILD=${1:?usage: profile.sh <build dir> <recipe> [user@]host [gmon.out path]}
RECIPE=${2:?usage: profile.sh <build dir> <recipe> [user@]host [gmon.out path]}
HOST=${3:?usage: profile.sh <build dir> <recipe> [user@]host [gmon.out path]}
GMON=${4:-/home/root/gmon.out}
NAME=$5
case "$HOST" in *@*) ;; *) HOST=root@$HOST ;; esac

WORK=$(ls -d "$BUILD"/tmp/work/*/"$RECIPE"/*/ 2>/dev/null | head -1)
[ -n "$WORK" ] || { echo "no work directory for $RECIPE under $BUILD/tmp/work" >&2; exit 1; }
GPROF=$(ls "$WORK"/recipe-sysroot-native/usr/bin/*/*-gprof 2>/dev/null | head -1)
[ -n "$GPROF" ] || { echo "no gprof in $WORK/recipe-sysroot-native" >&2; exit 1; }
# The named program, or the largest executable of the recipe, from the
# image directory, where the binaries still have their symbols
if [ -n "$NAME" ]; then
    BIN=$(find "$WORK/image" -type f -name "$NAME" | head -1)
else
    BIN=$(find "$WORK/image" -type f -perm -u+x -size +100k 2>/dev/null | xargs ls -S | head -1)
fi
[ -n "$BIN" ] || { echo "no binary in $WORK/image" >&2; exit 1; }

scp -O -q "$HOST:$GMON" "gmon-$RECIPE.out"
# A gmon.out left there would be read as the next program's profile
ssh "$HOST" "rm -f $GMON"
echo "binary: $BIN"
"$GPROF" -b "$BIN" "gmon-$RECIPE.out" > "profile-$RECIPE.txt"
head -40 "profile-$RECIPE.txt"
echo "... the whole profile with the call graph: profile-$RECIPE.txt"
