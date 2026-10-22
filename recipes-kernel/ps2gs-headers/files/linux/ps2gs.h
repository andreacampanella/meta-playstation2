/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * PlayStation 2 Graphics Synthesizer device for programs: /dev/ps2gs
 *
 * A program maps a packet buffer with mmap(). The driver allocates the
 * buffer at the first mmap() call, up to PS2GS_MAX_BUFFER bytes. The program
 * writes GIF packets (for the GS) or VIF1 packets (for vector unit 1) into
 * the buffer and sends them with PS2GS_IOC_SEND. The driver moves the data
 * with the DMA controller.
 *
 * The console text driver (ps2fb) shares the GS. Put the console in graphics
 * mode (KDSETMODE, KD_GRAPHICS) before drawing, so that the console does not
 * draw at the same time.
 */
#ifndef _UAPI_LINUX_PS2GS_H
#define _UAPI_LINUX_PS2GS_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define PS2GS_MAX_BUFFER	(1024 * 1024)

/* Channels for PS2GS_IOC_SEND and PS2GS_IOC_WAIT. */
#define PS2GS_CHANNEL_GIF	0	/* GIF: packets go to the GS (PATH3) */
#define PS2GS_CHANNEL_VIF1	1	/* VIF1: VIF codes, VU1 programs and data */

/* Flags for PS2GS_IOC_SEND. */
#define PS2GS_SEND_WAIT		(1 << 0)	/* Return when the DMA ends */

struct ps2gs_send {
	__u32 offset;		/* Byte offset in the buffer, multiple of 16 */
	__u32 qwc;		/* Number of 16-byte words */
	__u32 channel;		/* PS2GS_CHANNEL_* */
	__u32 flags;		/* PS2GS_SEND_* */
};

/* Flags for PS2GS_IOC_WAIT. */
#define PS2GS_WAIT_GIF		(1 << 0)	/* GIF DMA idle */
#define PS2GS_WAIT_VIF1		(1 << 1)	/* VIF1 DMA idle */
#define PS2GS_WAIT_VU1		(1 << 2)	/* VIF1 idle and VU1 program ended */

/* Privileged GS registers for PS2GS_IOC_GET_REG and PS2GS_IOC_SET_REG. */
#define PS2GS_REG_PMODE		0
#define PS2GS_REG_DISPFB1	1
#define PS2GS_REG_DISPLAY1	2
#define PS2GS_REG_DISPFB2	3
#define PS2GS_REG_DISPLAY2	4
#define PS2GS_REG_BGCOLOR	5
#define PS2GS_REG_CSR		6	/* Read only */

struct ps2gs_reg {
	__u32 reg;		/* PS2GS_REG_* */
	__u32 pad;
	__u64 value;
};

/*
 * PS2GS_IOC_RECEIVE: the GS writes qwc quadwords into the buffer at
 * offset, after the program sent a transfer setup with TRXDIR = 1
 * (local to host). Driver version 2 and later.
 */
struct ps2gs_receive {
	__u32 offset;		/* Byte offset in the buffer, a multiple of 16 */
	__u32 qwc;		/* Quadwords to receive */
};

struct ps2gs_info {
	__u32 version;		/* Driver version */
	__u32 max_buffer;	/* Largest buffer for mmap(), in bytes */
	__u32 buffer;		/* Size of the mapped buffer, 0 if none */
	__u32 vsync_irq;	/* 1 if the vertical sync interrupt works */
};

#define PS2GS_IOC_MAGIC		0x9a

#define PS2GS_IOC_INFO		_IOR(PS2GS_IOC_MAGIC, 0, struct ps2gs_info)
#define PS2GS_IOC_SEND		_IOW(PS2GS_IOC_MAGIC, 1, struct ps2gs_send)
#define PS2GS_IOC_WAIT		_IO(PS2GS_IOC_MAGIC, 2)	/* arg: PS2GS_WAIT_* */
#define PS2GS_IOC_VSYNC		_IO(PS2GS_IOC_MAGIC, 3)	/* returns the field */
#define PS2GS_IOC_GET_REG	_IOWR(PS2GS_IOC_MAGIC, 4, struct ps2gs_reg)
#define PS2GS_IOC_SET_REG	_IOW(PS2GS_IOC_MAGIC, 5, struct ps2gs_reg)
#define PS2GS_IOC_RESET		_IO(PS2GS_IOC_MAGIC, 6)	/* Reset GIF and VIF1 */
#define PS2GS_IOC_RECEIVE	_IOW(PS2GS_IOC_MAGIC, 7, struct ps2gs_receive)

#endif /* _UAPI_LINUX_PS2GS_H */
