// SPDX-License-Identifier: GPL-2.0
/*
 * SPU2 sound output: PCM from the kernel to the sound input of core 1.
 *
 * The module uses the sound library LIBSD of the console ROM. The kernel
 * driver loads rom0:LIBSD before this module. LIBSD runs a DMA loop over
 * a buffer of two halves. After each half, its callback signals the feed
 * thread. The thread fills the idle half with the next chunk of the ring,
 * or with silence when the ring is empty or the stream is stopped, and
 * tells the kernel how many bytes it consumed.
 *
 * The hardware wants each block of 1,024 bytes as 256 left samples then
 * 256 right samples. The ring holds interleaved frames, as ALSA gives
 * them, so the thread splits the channels when it copies a chunk.
 *
 * The protocol is in spu2-sif.h.
 *
 * The LIBSD constants and the sequence of LIBSD calls (init, the mixer
 * settings, the loop transfer with its callback) follow the audsrv module
 * and the libsd headers of the ps2sdk project (Academic Free License 2.0,
 * https://github.com/ps2dev/ps2sdk). The module structure (the thread, the
 * semaphore, the SIF command handler) follows the gamepad module of
 * iopmod by Fredrik Noring (GPL-2.0, https://github.com/frno7/iopmod),
 * whose build makes this module.
 */

#include "iopmod/errno.h"
#include "iopmod/interrupt.h"
#include "iopmod/irqs.h"
#include "iopmod/iop-error.h"
#include "iopmod/module.h"
#include "iopmod/printk.h"
#include "iopmod/sif.h"
#include "iopmod/sifcmd.h"
#include "iopmod/string.h"
#include "iopmod/thread.h"
#include "iopmod/typecheck.h"
#include "iopmod/types.h"

#include "iopmod/module-prototype.h"
#include "iopmod/module/libsd.h"
#include "iopmod/spu2-sif.h"

/* LIBSD constants, from the ps2sdk header libsd-common.h */
#define SD_CORE_0		0
#define SD_CORE_1		1
#define SD_PARAM_MVOLL		((0x09 << 8) | 0x80)	/* master volume */
#define SD_PARAM_MVOLR		((0x0a << 8) | 0x80)
#define SD_PARAM_AVOLL		((0x0d << 8) | 0x80)	/* external input */
#define SD_PARAM_AVOLR		((0x0e << 8) | 0x80)
#define SD_PARAM_BVOLL		((0x0f << 8) | 0x80)	/* sound input */
#define SD_PARAM_BVOLR		((0x10 << 8) | 0x80)
#define SD_TRANS_STOP		2
#define SD_TRANS_LOOP		0x10
#define SD_INIT_COLD		0
#define SD_VOLUME_MAX		0x3fff

/* The channel of the block transfer is the core number. */
#define SPU2_CHANNEL		SD_CORE_1

/* One hardware block: 256 frames */
#define SPU2_BLOCK		1024
#define SPU2_BLOCK_FRAMES	(SPU2_BLOCK / 4)

static u8 ring[SPU2_RING_SIZE] __attribute__((aligned(64)));

/* The LIBSD loop buffer: two halves of one chunk */
static u8 loop[2 * SPU2_CHUNK] __attribute__((aligned(64)));

static bool debug;		/* "debug" argument: the first events in the log */

static struct {
	volatile u32 written;	/* bytes the kernel put in the ring */
	volatile u32 consumed;	/* bytes moved to the hardware */
	volatile bool playing;
	volatile bool announce;	/* print the start, from the thread */
	volatile u32 misplaced;	/* chunks at an unexpected offset */
	int sema;
} stream;

/* Split a chunk of interleaved frames into hardware blocks. */
static void split_chunk(u8 *dst, const u8 *src)
{
	const s16 *frame = (const s16 *)src;
	s16 *out = (s16 *)dst;

	for (unsigned b = 0; b < SPU2_CHUNK / SPU2_BLOCK; b++) {
		s16 *left = out;
		s16 *right = out + SPU2_BLOCK_FRAMES;

		for (unsigned i = 0; i < SPU2_BLOCK_FRAMES; i++) {
			left[i] = frame[0];
			right[i] = frame[1];
			frame += 2;
		}
		out += 2 * SPU2_BLOCK_FRAMES;
	}
}

static void report_consumed(void)
{
	const struct spu2_consumed packet = { .consumed = stream.consumed };
	int err = sif_cmd_opt(SIF_CMD_SPU2, SPU2_OP_CONSUMED,
		&packet, sizeof(packet));

	if (err < 0)
		pr_err("spu2: sif_cmd failed with %d\n", err);
}

/* Runs in interrupt context after each half of the loop buffer. */
static int half_done(void *data)
{
	thsemap_isignal_sema(stream.sema);

	return 0;
}

static void feed(void *arg)
{
	bool first = true;

	for (;;) {
		thsemap_wait_sema(stream.sema);

		if (debug && first) {
			pr_info("spu2: the transfer loop runs\n");
			first = false;
		}
		if (debug && stream.announce) {
			stream.announce = false;
			pr_info("spu2: start with %u bytes in the ring, %u misplaced\n",
				stream.written - stream.consumed, stream.misplaced);
		}

		/* The DMA moves the other half now. This one is free. */
		const unsigned idle =
			1 - (sceSdBlockTransStatus(SPU2_CHANNEL, 0) >> 24);
		u8 *dst = &loop[idle * SPU2_CHUNK];

		if (stream.playing &&
		    stream.written - stream.consumed >= SPU2_CHUNK) {
			split_chunk(dst, &ring[stream.consumed % SPU2_RING_SIZE]);
			stream.consumed += SPU2_CHUNK;
			if (debug && stream.consumed == SPU2_CHUNK)
				pr_info("spu2: first chunk played\n");
			report_consumed();
		} else {
			memset(dst, 0, SPU2_CHUNK);
		}
	}
}

static void set_volume(u16 left, u16 right)
{
	if (left > SD_VOLUME_MAX)
		left = SD_VOLUME_MAX;
	if (right > SD_VOLUME_MAX)
		right = SD_VOLUME_MAX;
	sceSdSetParam(SD_CORE_1 | SD_PARAM_BVOLL, left);
	sceSdSetParam(SD_CORE_1 | SD_PARAM_BVOLR, right);
}

/*
 * Runs in interrupt context, when a command from the kernel arrives. No
 * message from here: a printk is a packet to the kernel, and the kernel
 * may be in a send that waits for this handler to return.
 */
static void spu2_cmd(const struct sif_cmd_header *header, void *arg)
{
	const void *payload = sif_cmd_payload(header);

	switch (header->opt) {
	case SPU2_OP_DATA: {
		const struct spu2_data *data = payload;

		if (data->offset != stream.written % SPU2_RING_SIZE)
			stream.misplaced++;
		stream.written += data->size;
		break;
	}
	case SPU2_OP_START:
		stream.playing = true;
		stream.announce = true;
		break;
	case SPU2_OP_STOP:
		stream.playing = false;
		stream.written = 0;
		stream.consumed = 0;
		break;
	case SPU2_OP_VOLUME: {
		const struct spu2_volume *volume = payload;

		set_volume(volume->left, volume->right);
		break;
	}
	default:
		stream.misplaced++;
	}
}

static void hello(void)
{
	const struct spu2_hello packet = {
		.ring = (u32)ring,
		.ring_size = SPU2_RING_SIZE,
	};
	int err = sif_cmd_opt(SIF_CMD_SPU2, SPU2_OP_HELLO,
		&packet, sizeof(packet));

	if (err < 0)
		pr_err("spu2: sif_cmd failed with %d\n", err);
}

/*
 * The mixer, as audsrv sets it: core 1 plays the sound input at full
 * master volume. Core 0 feeds core 1 (the external input) and is silent.
 */
static void init_mixer(void)
{
	sceSdSetParam(SD_CORE_0 | SD_PARAM_MVOLL, 0);
	sceSdSetParam(SD_CORE_0 | SD_PARAM_MVOLR, 0);
	sceSdSetParam(SD_CORE_0 | SD_PARAM_BVOLL, 0);
	sceSdSetParam(SD_CORE_0 | SD_PARAM_BVOLR, 0);
	sceSdSetParam(SD_CORE_1 | SD_PARAM_AVOLL, 0x7fff);
	sceSdSetParam(SD_CORE_1 | SD_PARAM_AVOLR, 0x7fff);
	sceSdSetParam(SD_CORE_1 | SD_PARAM_MVOLL, SD_VOLUME_MAX);
	sceSdSetParam(SD_CORE_1 | SD_PARAM_MVOLR, SD_VOLUME_MAX);
	set_volume(SD_VOLUME_MAX, SD_VOLUME_MAX);
}

static enum module_init_status spu2_init(int argc, char *argv[])
{
	static const struct iop_thread feed_thread = {
		.attr = THREAD_ATTR_C,
		.thread = feed,
		.stacksize = 2048,
		.priority = 0x20,	/* above the gamepad poll */
	};
	const struct iop_sema sema = { .initial = 0, .max = 1 };
	int thread, err;

	/* The kernel driver passes "debug" with its own debug parameter. */
	for (int i = 1; i < argc; i++)
		if (argv[i][0] == 'd' && argv[i][1] == 'e' && argv[i][2] == 'b')
			debug = true;

	if (sceSdInit(SD_INIT_COLD) < 0) {
		pr_err("spu2: sceSdInit failed\n");
		return MODULE_EXIT;
	}
	init_mixer();
	memset(loop, 0, sizeof(loop));

	stream.sema = thsemap_create_sema(&sema);
	if (stream.sema < 0) {
		pr_err("spu2: thsemap_create_sema failed with %d: %s\n",
			stream.sema, iop_error_message(stream.sema));
		return MODULE_EXIT;
	}

	thread = thbase_create(&feed_thread);
	if (thread < 0) {
		pr_err("spu2: thbase_create failed with %d: %s\n",
			thread, iop_error_message(thread));
		goto err_thread;
	}
	err = thbase_start(thread, NULL);
	if (err < 0) {
		pr_err("spu2: thbase_start failed with %d: %s\n",
			err, iop_error_message(err));
		goto err_start;
	}

	sif_request_cmd(SIF_CMD_SPU2, spu2_cmd, NULL);

	sceSdSetTransCallback(SPU2_CHANNEL, half_done);
	if (sceSdBlockTrans(SPU2_CHANNEL, SD_TRANS_LOOP, loop, sizeof(loop)) < 0) {
		pr_err("spu2: sceSdBlockTrans failed\n");
		goto err_trans;
	}

	/*
	 * The DMA interrupt of the channel. The ROM LIBSD registers its
	 * handler for it but leaves the line off: the first half went out
	 * and no interrupt came (ICR2 bit 16 was clear). With the line on,
	 * the callback runs 94 times a second.
	 */
	err = intrman_enable_irq(IRQ_IOP_DMA_SPU2);
	if (err < 0)
		pr_warn("spu2: intrman_enable_irq gave %d\n", err);

	hello();
	pr_info("spu2: ready, ring of %u bytes\n", (unsigned)SPU2_RING_SIZE);

	return MODULE_RESIDENT;

err_trans:
	sif_release_cmd(SIF_CMD_SPU2);
	sceSdSetTransCallback(SPU2_CHANNEL, NULL);
	thbase_terminate(thread);
err_start:
	thbase_delete(thread);
err_thread:
	thsemap_delete_sema(stream.sema);
	return MODULE_EXIT;
}
module_init(spu2_init);
