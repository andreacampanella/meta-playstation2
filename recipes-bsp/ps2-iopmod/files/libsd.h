// SPDX-License-Identifier: GPL-2.0
/*
 * The sound driver library LIBSD of the console ROM (rom0:LIBSD).
 *
 * The ordinals follow the export table of the module, as listed in the
 * ps2sdk project (iop/sound/libsd/src/exports.tab; ps2sdk is under the
 * Academic Free License 2.0, https://github.com/ps2dev/ps2sdk). Version
 * 1.00 links with the library of every console. The functions are
 * documented in the ps2sdk header libsd-common.h.
 */

MODULE_ID(libsd, 0x0100);
LIBRARY_ID(libsd, 0x0100);

/* sceSdInit flag: 0 resets the SPU2 and clears its memory */
id_(4) int sceSdInit(int flag);

/* Entry: a SD_PARAM_ value with the core number in bit 0 */
id_(5) void sceSdSetParam(u16 entry, u16 value);
id_(6) u16 sceSdGetParam(u16 entry);

id_(11) void sceSdSetCoreAttr(u16 entry, u16 value);
id_(12) u16 sceSdGetCoreAttr(u16 entry);

/*
 * Block transfer: the DMA of the IOP that feeds the sound input of a core.
 * With mode SD_TRANS_LOOP the two halves of the buffer repeat forever and
 * the transfer callback runs, in interrupt context, after each half.
 */
id_(18) int sceSdBlockTrans(s16 channel, u16 mode, u8 *iop_addr, u32 size, ...);

/* Bits 24 and up: the half in transfer. Bits 0 to 23: the DMA address. */
id_(20) u32 sceSdBlockTransStatus(s16 channel, s16 flag);

typedef int (*sd_intr_callback)(void *data);
id_(21) sd_intr_callback sceSdSetTransCallback(s32 channel, sd_intr_callback cb);
