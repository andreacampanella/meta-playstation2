#!/bin/sh
# Count the software double operations in a program built for the R5900.
# The R5900 FPU has no double, so each operation on a double is a call to
# a libgcc function (__adddf3, __muldf3, __extendsfdf2, ...), 400 to
# 3,000 ns a time. Most of them in game code are accidents: "x * 0.5"
# promotes a float to a double because 0.5 is a double constant.
#
# usage: tools/doubles.sh <build dir> <recipe> [top]
#
#   build dir  the bitbake build directory (with tmp/work)
#   recipe     the recipe name (quakespasm, jfduke3d-polymost, ...)
#   top        how many functions to list (default 30)
#
# Prints the total number of soft-double calls and the functions that
# make the most. A function high on the list, in a hot path, is where a
# constant needs an f suffix, or where -fsingle-precision-constant
# helps. Run it before and after a change in the compiler flags.
set -e
BUILD=${1:?usage: doubles.sh <build dir> <recipe> [top]}
RECIPE=${2:?usage: doubles.sh <build dir> <recipe> [top]}
TOP=${3:-30}

WORK=$(ls -d "$BUILD"/tmp/work/*/"$RECIPE"/*/ 2>/dev/null | head -1)
[ -n "$WORK" ] || { echo "no work directory for $RECIPE under $BUILD/tmp/work" >&2; exit 1; }
OBJDUMP=$(ls "$WORK"/recipe-sysroot-native/usr/bin/*/*-objdump 2>/dev/null | head -1)
[ -n "$OBJDUMP" ] || { echo "no objdump in $WORK/recipe-sysroot-native" >&2; exit 1; }

# The unstripped binaries of the package
for BIN in $(find "$WORK/package" -type f -perm -u+x 2>/dev/null); do
    file "$BIN" | grep -q "ELF.*MIPS" || continue
    echo "== ${BIN#"$WORK"/package}"
    "$OBJDUMP" -d "$BIN" | awk -v top="$TOP" '
        /^[0-9a-f]+ <.*>:$/ { fn = $2; gsub(/[<>:]/, "", fn) }
        /\tjal\t.*<__(add|sub|mul|div|neg|eq|ne|lt|le|gt|ge|unord)df[23](@plt)?>|\tjal\t.*<__(extendsfdf2|truncdfsf2|fixdfsi|fixunsdfsi|floatsidf|floatunsidf|fixdfdi|floatdidf)(@plt)?>/ {
            n[fn]++; total++
        }
        END {
            printf "%d soft-double calls\n", total
            for (f in n) printf "%6d %s\n", n[f], f | "sort -rn | head -" top
        }'
done
