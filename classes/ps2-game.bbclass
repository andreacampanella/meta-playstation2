#
# Build flags for the games: speed before hardening. A recipe that
# inherits this class builds
#
#   - with -O3 instead of -O2,
#   - as a position-dependent executable (no PIE): a PIE program reaches
#     each global through the global offset table, one load more, and an
#     engine such as Polymost keeps its state in globals,
#   - without the stack protector (a canary check in each function with
#     an array) and without _FORTIFY_SOURCE (checked string functions),
#   - without -z now (every symbol resolved at start).
#
# The shared libraries (glibc, SDL2, libGL) keep the Poky defaults. A game
# that misbehaves with these flags can go back to the Poky defaults with
#
#     PS2_GAME_FLAGS:pn-<recipe> = "0"
#
# in local.conf. A recipe whose build system sets its own optimization
# level after the Poky flags passes PS2_GAME_OPT to it.

PS2_GAME_FLAGS ?= "1"
PS2_GAME_OPT = "${@'-O3' if d.getVar('PS2_GAME_FLAGS') == '1' else '-O2'}"

OVERRIDES:append = "${@':ps2-game-flags' if d.getVar('PS2_GAME_FLAGS') == '1' else ''}"

FULL_OPTIMIZATION:ps2-game-flags = "-O3 -pipe ${DEBUG_FLAGS}"
SECURITY_CFLAGS:ps2-game-flags = "${SECURITY_NOPIE_CFLAGS}"
SECURITY_LDFLAGS:ps2-game-flags = ""
