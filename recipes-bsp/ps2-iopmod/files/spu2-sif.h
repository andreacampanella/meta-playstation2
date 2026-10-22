/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The SIF protocol between the kernel sound driver (snd-ps2) and the IOP
 * module spu2. One file for both sides.
 *
 * The IOP module owns a ring of PCM samples: 48 kHz, 16-bit, stereo,
 * interleaved (left, right). The kernel writes chunks of SPU2_CHUNK bytes
 * into the ring with SIF DMA, then sends a DATA command. The module moves
 * one chunk at a time to the sound input of SPU2 core 1 and reports the
 * number of bytes it consumed with a CONSUMED command. All counters are
 * bytes since the last START and count up without a wrap in the ring.
 */
#ifndef PS2_SPU2_SIF_H
#define PS2_SPU2_SIF_H

/*
 * The SIF command number, in both directions. The kernel lists the others
 * in sif.h. The system command table of the IOP has 32 entries, so a
 * command the IOP receives must have a number below 0x20.
 */
#define SIF_CMD_SPU2		(0x80000000 | 0x14)

/* One chunk: 512 frames, 10.67 ms. The hardware takes blocks of 1024 bytes. */
#define SPU2_CHUNK		2048

/* The ring in IOP memory: 16 chunks, 171 ms. The IOP has 2 MiB. */
#define SPU2_RING_SIZE		(16 * SPU2_CHUNK)

/* The opt field of the command header */
enum spu2_op {
	/* kernel to IOP */
	SPU2_OP_DATA = 1,	/* struct spu2_data: a chunk is in the ring */
	SPU2_OP_START,		/* play from the ring */
	SPU2_OP_STOP,		/* play silence, forget the ring */
	SPU2_OP_VOLUME,		/* struct spu2_volume */
	/* IOP to kernel */
	SPU2_OP_HELLO = 16,	/* struct spu2_hello: the module is ready */
	SPU2_OP_CONSUMED,	/* struct spu2_consumed */
};

struct spu2_data {
	u32 offset;		/* in the ring, for a check */
	u32 size;		/* bytes, a multiple of SPU2_CHUNK */
};

struct spu2_volume {
	u16 left;		/* 0 to 0x3fff */
	u16 right;
};

struct spu2_hello {
	u32 ring;		/* IOP address of the ring */
	u32 ring_size;		/* bytes */
};

struct spu2_consumed {
	u32 consumed;		/* bytes since START */
};

#endif /* PS2_SPU2_SIF_H */
