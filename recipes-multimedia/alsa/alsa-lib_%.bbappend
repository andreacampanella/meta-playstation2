# alsa-lib with 32-bit time, to match kernel 5.4.
#
# Yocto builds 32-bit MIPS user space with a 64-bit time_t. alsa-lib then
# uses the 64-bit-time layout of the ALSA ioctls (status, sync_ptr, control
# values, timers), which kernel 5.6 added. Kernel 5.4 answers ENOTTY, and no
# sound device opens. alsa-lib is the only program that talks to those
# ioctls, so it alone gets 32-bit time, as Yocto does for PulseAudio in
# time64.inc. The large file offsets stay.
#
# The eight API functions that return a timestamp (snd_pcm_htimestamp,
# snd_pcm_status_get_*tstamp, snd_timer_status_get_timestamp) then write a
# 32-bit timespec into a program's 64-bit one. SDL2 and the games call
# none of them. aplay calls one for the time in its xrun message. The port
# of the 5.6 kernel series removes this.
#
# Two switches, because autoconf 2.72 turns on 64-bit time by itself in
# AC_SYS_LARGEFILE (_TIME_BITS in config.h), whatever the compiler flags.
GLIBC_64BIT_TIME_FLAGS = " -D_FILE_OFFSET_BITS=64"
EXTRA_OECONF += "--disable-year2038"
INSANE_SKIP:${PN}:append = " 32bit-time"
