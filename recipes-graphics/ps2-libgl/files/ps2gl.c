// SPDX-License-Identifier: MIT
/*
 * ps2gl: OpenGL 1.1 (fixed function subset) for the PlayStation 2 under Linux.
 * The library is libGL.so.1. Programs load it as a normal OpenGL library.
 *
 * How a frame gets to the screen:
 *
 *   1. The library opens /dev/ps2gs and maps a packet buffer.
 *   2. Each glBegin/glEnd batch goes into the buffer as VIF1 codes: the
 *      vertices unpack into the data memory of vector unit 1 (VU1), then a
 *      call starts the VU1 program.
 *   3. VU1 does this for each vertex: multiply by the matrix, set clip
 *      flags, divide by w, change to screen coordinates, and scale the
 *      texture coordinates. Then VU1 sends the packet to the Graphics
 *      Synthesizer (GS).
 *   4. Texture uploads and register settings go to the GS directly through
 *      the VIF1 (DIRECT codes).
 *   5. The kernel moves the buffer with DMA. At the swap, the library waits
 *      for the vertical sync and shows the finished frame buffer. The next
 *      frame goes to the other buffer.
 *
 * The library owns the GS memory while a context exists: two frame buffers,
 * one depth buffer, and the rest for textures. Textures that do not fit go
 * out in least-recently-used order. The library keeps a copy of each texture
 * in RAM and loads it again when a program uses it.
 *
 * VU1 (program source: vu1.vsm, assembled by tools/vuasm.py into vu1.h)
 * transforms the vertices, drops a primitive with all corners outside one
 * plane, cuts the primitives that cross the near plane, the far plane or the
 * guard band (GUARD times the screen size), and projects the rest. It also
 * calculates the fog factor of each vertex. The GS cuts the picture at the
 * screen edge.
 *
 * Not in this version: mipmaps (levels above 0 are ignored), lighting,
 * display lists, face culling, line width, point size.
 *
 * Environment variables. Settings:
 *
 *   PS2GL_SIZE=WxH         window size for ps2glInit (default 320x240)
 *   PS2GL_FB16=0|1         16-bit frame and depth buffers (default: above
 *                          400x300)
 *   PS2GL_VSYNC=0          no wait for the vertical sync
 *   PS2GL_TEX8=0           no 8-bit textures with a CLUT (default: on)
 *   PS2GL_TEX32=1          32-bit textures (default: 16-bit)
 *   PS2GL_MAGV=N           vertical magnification of the display
 *   PS2GL_FOG=0|half|linear|exp|exp2  no fog, or a fixed test fog
 *   PS2GL_BUFFER=KiB       packet buffer size (default 256)
 *   PS2GL_UPLOAD_ROWS=N    rows in one texture transfer (default: what fits)
 *
 * Diagnostics:
 *
 *   PS2GL_VERBOSE=1|2      the display settings and the texture memory at
 *                          exit; 2 also prints the frame report once a second
 *   PS2GL_HUD=1            the frame report on the screen, with a graph
 *   PS2GL_CHECK=1          build every batch header and report a reuse
 *                          that would have been wrong
 *   PS2GL_DUMP=file        record the stream to a file;
 *   PS2GL_DUMP_FRAMES=N    how many frames (default 20);
 *   PS2GL_DUMP_START=N     after how many frames (default 0)
 *   PS2GL_FAKE=1           no hardware: a buffer in RAM and a check of the
 *                          stream, for a build on a PC
 *   PS2GL_NOSEND=1         build the packets and drop them (the CPU cost)
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <linux/kd.h>
#include <linux/ps2gs.h>

#include <GL/gl.h>

/* --- hardware constants --------------------------------------------------- */

/* GS register addresses for the address + data format */
#define GS_PRIM		0x00
#define GS_RGBAQ	0x01
#define GS_UV		0x03
#define GS_XYZ2		0x05
#define GS_TEX0_2	0x07
#define GS_CLAMP_2	0x09
#define GS_TEX1_2	0x15
#define GS_XYOFFSET_2	0x19
#define GS_PRMODECONT	0x1a
#define GS_MIPTBP1_2	0x35
#define GS_MIPTBP2_2	0x37
#define GS_TEXA		0x3b
#define GS_FOGCOL	0x3d
#define GS_TEXFLUSH	0x3f
#define GS_SCISSOR_2	0x41
#define GS_ALPHA_2	0x43
#define GS_DIMX		0x44
#define GS_DTHE		0x45
#define GS_COLCLAMP	0x46
#define GS_TEST_2	0x48
#define GS_PABE		0x49
#define GS_FBA_2	0x4b
#define GS_FRAME_2	0x4d
#define GS_ZBUF_2	0x4f
#define GS_BITBLTBUF	0x50
#define GS_TRXPOS	0x51
#define GS_TRXREG	0x52
#define GS_TRXDIR	0x53
#define GS_NOP		0x0f

/* GS pixel formats */
#define PSMCT32		0x00
#define PSMCT16		0x02
#define PSMT8		0x13	/* 8-bit indices with a 256-entry CLUT */
#define PSMZ24		0x01	/* ZBUF has 4 bits for PSM: PSMZ24 0x31 is 0x1 there */
#define PSMZ16		0x02	/* ZBUF has 4 bits for PSM: PSMZ16 0x32 is 0x2 there */

/* VIF1 codes. The data of an UNPACK or DIRECT code follows the code. */
#define VIF_NOP		0x00000000u
#define VIF_STCYCL	0x01000101u	/* CL = WL = 1 */
#define VIF_FLUSHE	0x10000000u	/* Wait for the end of the VU1 program */
#define VIF_FLUSH	0x11000000u	/* Also wait for the transfer to the GS */
#define VIF_MSCAL(a)	(0x14000000u | ((a) & 0xffffu))
#define VIF_MPG(n, a)	(0x4a000000u | (((n) & 0xffu) << 16) | ((a) & 0xffffu))
#define VIF_DIRECT(q)	(0x50000000u | ((q) & 0xffffu))
#define VIF_UNPACK(n, a) (0x6c000000u | (((n) & 0xffu) << 16) | ((a) & 0x3ffu))

/*
 * VU1 data memory layout for a vertex batch, in quadwords. The addresses
 * come from the program source (vu1.vsm) through the generated header.
 *
 *    0 -  3  clip matrix, one column in each quadword
 *    4       screen scale x, y, z
 *    5       screen offset x, y, z
 *    6       parameters: word 0 = vertex count, 1 = kind, 2 = fog mode
 *    7       fog row: fog distance = fogrow . (x, y, z, 1)
 *    8       fog parameters A, B, and the constants 255, 1
 *    9       GIF tag for GS register settings
 *   10 - 22  thirteen register settings
 *   23       GIF tag for the vertices (NLOOP 0, VU1 fills it in)
 *   24 -     three quadwords for each vertex:
 *              ST:    s, t, 1.0
 *              RGBAQ: red, green, blue, alpha as integers
 *              XYZ:   float x, y, z
 *   The output buffers, the clipper buffers and the clip planes follow.
 */
#include "vu1.h"

#define Q_SCALE		VU_Q_SCALE
#define Q_OFFSET	VU_Q_OFFSET
#define Q_PARAM		VU_Q_PARAM
#define Q_FOGROW	VU_Q_FOGROW
#define Q_FOGPARAM	VU_Q_FOGPARAM
#define Q_PACKET	VU_Q_SETTINGS
#define Q_SETTINGS	13	/* Number of register settings */
#define Q_VERTEX_TAG	VU_Q_VERTEX_TAG
#define Q_VERTEX	VU_Q_VERTEX
#define Q_HEADER	VU_Q_VERTEX	/* Quadwords before the vertices */
#define Q_PLANES	VU_Q_PLANES
#define VERTEX_WORDS	12
#define MAX_VERTICES	VU_MAX_VERTICES	/* A multiple of 6 */

/* Primitive kinds for the VU1 program */
#define KIND_POINTS		0
#define KIND_LINES		1
#define KIND_LINE_STRIP		2
#define KIND_TRIANGLES		3
#define KIND_TRIANGLE_STRIP	4
#define KIND_FANS		5	/* Triangles, quads and fans: bit 0x40 starts a fan */
#define FAN_START		0x40

/* Fog modes for the VU1 program */
#define FOG_NONE	0
#define FOG_LINEAR	1
#define FOG_EXP		2
#define FOG_EXP2	3

#define BIAS 1024

/* Reject a triangle with a corner beyond GUARD times the half screen size. */
#define GUARD 4.0f

#define GS_PAGES	512	/* 8 KiB each */
#define PAGE_FRAME	0xfffe	/* Page owner: frame or depth buffer */
#define PAGE_CLUT	0xfffd	/* Page owner: eight CLUTs of 1 KiB */
#define PAGE_HUD	0xfffc	/* Page owner: the font of the HUD */
#define CLUT_PAGES	64	/* At most this many pages of CLUTs */
#define MAX_TEXTURES	2048
#define MAX_TEXTURE_SIZE 512
#define STREAM_BYTES	(1024 * 1024)

/* --- the device and the packet stream ------------------------------------ */

static struct {
	int ready;
	int fd;				/* /dev/ps2gs */
	unsigned driver_version;	/* From PS2GS_IOC_INFO: 2 has the receive, 3 the uncached buffer */
	uint32_t *stream;		/* The mapped packet buffer */
	size_t stream_bytes;
	size_t used, capacity;		/* In 32-bit words */
	/*
	 * The buffer is in two halves. The library fills one while the DMA
	 * reads the other: a send does not wait, and the kernel starts a
	 * send after the previous one has ended, so the half it reads is
	 * never the half the library writes. base and limit are the half in
	 * use. region is its size.
	 */
	size_t base, limit, region;
	int tex_dirty;			/* An upload since the last TEXFLUSH */
	int nosend;			/* PS2GL_NOSEND=1: drop the packets */
	int check_header;		/* PS2GL_CHECK=1: build every header and compare */

	int width, height;		/* Frame buffer size */
	int fbw;			/* Frame buffer width in 64-pixel units */
	int frame_page[2], zbuf_page;
	int draw;			/* Index of the frame buffer that gets drawn */
	int vsync;
	uint64_t display1;		/* The display registers of the console */
	int have_display1;

	uint16_t page_owner[GS_PAGES];	/* 0 = free, else texture id or PAGE_FRAME */
	unsigned use_counter;

	int tty, graphics_mode;
	int keyboard[8], keyboards;
	int tex16;			/* 1: textures use 16 bits for each pixel */
	int fb16;			/* 1: 16-bit frame buffers and a 16-bit depth buffer */
	float zscale;			/* The depth range: 2^24 or 2^16 */
	int verbose;
	int fake;			/* PS2GL_FAKE=1: no device, check the stream */
	unsigned long fake_sends, fake_words;
	/*
	 * PS2GL_VERBOSE=2: a line once a second with what the frames cost.
	 * PS2GL_HUD=1: the same numbers on the screen (see the HUD below).
	 * report: the counters and the clocks run. report_print: the line.
	 */
	int report, report_print, hud;
	size_t upload_rows;		/* PS2GL_UPLOAD_ROWS: at most this many rows a transfer */
	unsigned long r_sends, r_words, r_uploads, r_upload_words, r_evictions, r_batches, r_frames;
	unsigned long r_corners;
	double r_time;
	/* Time inside the library: between glBegin and glEnd, and in the swap */
	double r_in_gl, r_in_swap, r_in_send, r_t0;
	double r_read;		/* The cost of a clock read, in the sample being taken */
	double clock_cost;	/* What a now_seconds() call takes: taken off each sample */
	unsigned r_sample;
	int r_sampled;
} hw;

/*
 * Workstation test: read the stream as the VIF1 and the GIF would, and
 * report a code or a packet that is not well formed.
 */
static void stream_check(const uint32_t *w, size_t words)
{
	size_t i = 0;

	while (i < words) {
		const uint32_t code = w[i];
		const unsigned cmd = (code >> 24) & 0x7f;
		const unsigned num = (code >> 16) & 0xff;
		size_t data;

		if (code >> 31)
			fprintf(stderr, "ps2gl check: interrupt bit at word %zu\n", i);
		switch (cmd) {
		case 0x00: case 0x01: case 0x10: case 0x11: case 0x13:
			i++;
			break;
		case 0x14:
			if ((code & 0xffff) != VU_LABEL_START)
				fprintf(stderr, "ps2gl check: MSCAL address %u at word %zu\n",
					code & 0xffff, i);
			i++;
			break;
		case 0x4a:
			if ((i + 1) % 4 != 0)
				fprintf(stderr, "ps2gl check: MPG data not aligned at word %zu\n", i);
			i += 1 + 2 * (num ? num : 256);
			break;
		case 0x50: {
			const size_t qwc = (code & 0xffff) ? (code & 0xffff) : 65536;
			size_t at;

			if ((i + 1) % 4 != 0)
				fprintf(stderr, "ps2gl check: DIRECT data not aligned at word %zu\n", i);
			at = i + 1;
			data = at + 4 * qwc;
			while (at < data) {
				const uint64_t tag = (uint64_t)w[at] | ((uint64_t)w[at + 1] << 32);
				const unsigned nloop = tag & 0x7fff, flg = (tag >> 58) & 3;
				const unsigned nreg = (tag >> 60) & 0xf;
				size_t size;

				if (flg == 2)
					size = nloop;
				else if (flg == 0)
					size = (size_t)nloop * (nreg ? nreg : 16);
				else
					size = (size_t)nloop * (nreg ? nreg : 16) / 2;
				at += 4 * (1 + size);
			}
			if (at != data)
				fprintf(stderr, "ps2gl check: DIRECT packets do not match %zu words at word %zu\n",
					4 * qwc, i);
			i = data;
			break;
		}
		case 0x6c:
			i += 1 + 4 * (num ? num : 256);
			break;
		default:
			fprintf(stderr, "ps2gl check: unknown code %08x at word %zu\n", code, i);
			return;
		}
	}
	if (i != words)
		fprintf(stderr, "ps2gl check: stream ends inside a code\n");
}

/*
 * Hooks for a software model of the GS on a PC: in fake mode, a model
 * linked into the program can take the stream and the register writes.
 * The library has no model itself, so these symbols are weak and stay
 * NULL in a normal build.
 */
void ps2gl_model_init(int width, int height) __attribute__((weak));
void ps2gl_model_stream(const uint32_t *words, size_t count) __attribute__((weak));
void ps2gl_model_reg(uint32_t reg, uint64_t value) __attribute__((weak));
size_t ps2gl_model_receive(uint32_t *words, size_t count) __attribute__((weak));

/*
 * PS2GL_DUMP=file writes every send and every display register write to the
 * file, for PS2GL_DUMP_FRAMES frames (20 by default), for a look at the
 * packets on a PC. Record: a 32-bit kind (1 = stream,
 * 2 = register), a 32-bit count, then the words.
 */
static FILE *dump_file;
static int dump_frames_left = -1;
static int dump_skip;		/* PS2GL_DUMP_START: frames before the dump starts */
static int dump_prologue;	/* The dump has started: send the context's settings again */
static void init_stream(void);

static void dump_open(void)
{
	const char *path = getenv("PS2GL_DUMP"), *frames = getenv("PS2GL_DUMP_FRAMES");

	if (!path || dump_file)
		return;
	dump_file = fopen(path, "wb");
	dump_frames_left = frames ? atoi(frames) : 20;
	dump_skip = getenv("PS2GL_DUMP_START") ? atoi(getenv("PS2GL_DUMP_START")) : 0;
	if (dump_file)
		fprintf(stderr, "ps2gl: dump of %d frames to %s, after %d\n",
			dump_frames_left, path, dump_skip);
}

static void dump_close(void)
{
	if (dump_file)
		fclose(dump_file);
	dump_file = NULL;
}

static void dump_stream(const uint32_t *words, size_t count)
{
	uint32_t head[2] = { 1, (uint32_t)count };

	if (!dump_file || dump_frames_left == 0 || dump_skip)
		return;
	fwrite(head, sizeof(head), 1, dump_file);
	fwrite(words, sizeof(*words), count, dump_file);
}

static void dump_reg(uint32_t reg, uint64_t value)
{
	uint32_t rec[4] = { 2, 2, reg, 0 };

	if (!dump_file || dump_frames_left == 0)
		return;
	if (dump_skip) {
		/*
		 * The frames before the start: only the count of them. When
		 * the count is up, the next frame starts with the settings of
		 * the context and the VU1 program again, so the replay has
		 * them (see ps2glSwapBuffers).
		 */
		if (reg == PS2GS_REG_DISPFB2 && --dump_skip == 0)
			dump_prologue = 1;
		return;
	}
	rec[3] = (uint32_t)value;
	fwrite(rec, sizeof(rec), 1, dump_file);
	rec[0] = (uint32_t)(value >> 32);
	fwrite(rec, sizeof(rec[0]), 1, dump_file);
	if (reg == PS2GS_REG_DISPFB2 && dump_frames_left > 0 && --dump_frames_left == 0) {
		fprintf(stderr, "ps2gl: dump complete\n");
		dump_close();
	}
}

static int kind_is_list(unsigned kind);
static void batch_start(void);
static void batch_close(void);
static void stream_sync(void);
struct texture;
static void texture_size(struct texture *t, int format);
static double now_seconds(void);

/* The other half of the buffer becomes the one in use */
static void stream_switch(void)
{
	hw.base = hw.base ? 0 : hw.region;
	hw.limit = hw.base + hw.region;
	hw.used = hw.base;
}

/*
 * Send the half in use. wait: return after the DMA (the end of a frame).
 * Without wait, the kernel starts the DMA after the previous one has
 * ended and returns at once, and the library goes on in the other half.
 */
static void stream_send(int wait)
{
	struct ps2gs_send s;
	size_t words;

	batch_close();
	words = hw.used - hw.base;
	if (!words)
		return;
	while (words & 3) {
		hw.stream[hw.used++] = VIF_NOP;
		words++;
	}
	/*
	 * Kernel 3 and later map the buffer uncached accelerated: a store
	 * waits in the write buffer of the CPU and goes to memory in a
	 * burst. A sync empties it, so that a read of the buffer here, and
	 * the DMA after the ioctl, see every word.
	 */
	stream_sync();
	dump_stream(hw.stream + hw.base, words);

	hw.r_sends++;
	hw.r_words += words;
	if (hw.fake) {
		/* PS2GL_NOSEND=1 with PS2GL_FAKE=1: the library against an ordinary buffer */
		if (!hw.nosend) {
			stream_check(hw.stream + hw.base, words);
			if (ps2gl_model_stream)
				ps2gl_model_stream(hw.stream + hw.base, words);
		}
		hw.fake_sends++;
		hw.fake_words += words;
		stream_switch();
		return;
	}
	s.offset = hw.base * 4;
	s.qwc = words / 4;
	s.channel = PS2GS_CHANNEL_VIF1;
	s.flags = wait ? PS2GS_SEND_WAIT : 0;
	if (hw.nosend) {
		/* PS2GL_NOSEND=1: the CPU cost alone, for a measurement */
	} else {
		const double t0 = hw.report ? now_seconds() : 0.0;

		if (ioctl(hw.fd, PS2GS_IOC_SEND, &s) < 0)
			fprintf(stderr, "ps2gl: send: %s\n", strerror(errno));
		if (hw.report)
			hw.r_in_send += now_seconds() - t0;
	}
	stream_switch();
}

/* The write buffer of the CPU to memory, before a read of the stream or a DMA */
static void stream_sync(void)
{
	if (hw.driver_version >= 3)
		__sync_synchronize();
}

/* Make room for words in the half in use, with a send if it is full. */
static uint32_t *stream_reserve(size_t words)
{
	if (hw.used + words > hw.limit)
		stream_send(0);
	if (hw.used + words > hw.limit)
		return NULL;
	hw.used += words;
	return &hw.stream[hw.used - words];
}

static void stream_word(uint32_t w)
{
	uint32_t *p = stream_reserve(1);

	if (p)
		*p = w;
}

/* Pad with NOP codes so that the next word starts a quadword. */
static void stream_align(void)
{
	while (hw.used & 3)
		stream_word(VIF_NOP);
}

/*
 * A DIRECT code and the GIF tag of an address + data packet with n
 * registers. The codes and the packet come from one reservation: a
 * reservation can send the buffer and start at its beginning, and the
 * codes must not go out in one send with the packet in the next. (That
 * split put the DIRECT count of stream_direct_end four bytes before the
 * buffer, and a stale word on a random GS register.)
 */
static uint64_t *stream_direct_ad(unsigned n, int end)
{
	uint32_t *w;
	uint64_t *q;

	/* The open batch first: its pages or its state may change below */
	batch_close();
	/* FLUSH: the VU1 program and its packet end before the direct data */
	stream_align();
	w = stream_reserve(4 + 4 * (n + 1));
	if (!w)
		return NULL;
	w[0] = VIF_FLUSH;
	w[1] = VIF_NOP;
	w[2] = VIF_NOP;
	w[3] = VIF_DIRECT(n + 1);
	q = (uint64_t *)(w + 4);
	q[0] = (uint64_t)n | (end ? 1ull << 15 : 0) | (1ull << 60);
	q[1] = 0xe;
	return q + 2;
}

static void ad(uint64_t *q, unsigned addr, uint64_t data)
{
	q[0] = data;
	q[1] = addr;
}

/*
 * Close a direct packet (from stream_direct_ad) at the register after the
 * last one written: the exact count goes into the GIF tag and the DIRECT
 * code, and the room not used goes back. A count written by hand that is
 * one too large sends a stale word of the buffer to a random GS register.
 */
static void stream_direct_end(uint64_t *first, uint64_t *end)
{
	uint64_t *tag = first - 2;
	uint32_t *code = (uint32_t *)tag - 1;
	const unsigned n = (unsigned)(end - first) / 2;

	tag[0] = (tag[0] & ~0x7fffull) | n;
	*code = VIF_DIRECT(n + 1);
	hw.used = (size_t)((uint32_t *)end - hw.stream);
}

/* --- GS memory ----------------------------------------------------------- */

struct texture {
	int used;
	int width, height;
	int tw, th;		/* log2 of width and height */
	int tbw;		/* Buffer width in 64-pixel units */
	int page, pages;	/* GS memory, if resident */
	int resident;
	int linear, clamp;
	int format;		/* PSMCT32, PSMCT16 or PSMT8 */
	void *copy;		/* The pixels in RAM, in the GS format */
	size_t copy_bytes;
	unsigned last_use;
	/*
	 * PSMT8: the pixels are indices into clut, 256 colors in the GS 32-bit
	 * format. clut_slot is the place of the CLUT in GS memory while the
	 * texture is resident. lum_slot holds a second CLUT with the
	 * brightness in alpha, for the multiply blend (see lum below).
	 */
	uint32_t *clut;
	int clut_colors;
	int clut_slot, lum_slot, lum_slot_scale;
	/*
	 * Multiply blending: the GS multiplies a color by an alpha, not by a
	 * color. For glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR) and (GL_ZERO,
	 * GL_SRC_COLOR), the library draws a second copy of the texture that
	 * has the brightness in the alpha channel. lum_scale is 1 or 2. The
	 * copy is a PSMT8 texture with a gray ramp. A PSMT8 texture needs no
	 * copy: it gets a second CLUT (lum_slot).
	 */
	struct texture *lum;
	int lum_scale;
	int is_lum;
};

static struct texture textures[MAX_TEXTURES + 1];
static struct texture lum_textures[MAX_TEXTURES + 1];

/* A texture with no pixels yet */
static void texture_init(struct texture *t)
{
	memset(t, 0, sizeof(*t));
	t->used = 1;
	t->linear = 1;
	t->clut_slot = -1;
	t->lum_slot = -1;
}

static uint16_t texture_owner(const struct texture *t)
{
	if (t->is_lum)
		return (uint16_t)(MAX_TEXTURES + 1 + (t - lum_textures));
	return (uint16_t)(t - textures);
}

static void pages_free(int first, int count)
{
	int i;

	for (i = first; i < first + count; i++)
		hw.page_owner[i] = 0;
}

static int pages_find(int count)
{
	int run = 0, i;

	for (i = 0; i < GS_PAGES; i++) {
		run = hw.page_owner[i] ? 0 : run + 1;
		if (run == count)
			return i - count + 1;
	}
	return -1;
}

static void clut_free(int slot);

/* Counts the state changes that can change a batch header. See batch_open(). */
static unsigned state_serial = 1;

static void texture_evict(struct texture *t)
{
	state_serial++;
	if (t->resident) {
		hw.r_evictions++;
		pages_free(t->page, t->pages);
		t->resident = 0;
	}
	clut_free(t->clut_slot);
	clut_free(t->lum_slot);
	t->clut_slot = -1;
	t->lum_slot = -1;
}

/* Free the pages of the texture with the oldest use. Return 0 if none. */
static int evict_oldest(void)
{
	struct texture *oldest = NULL;
	int i;

	for (i = 1; i <= MAX_TEXTURES; i++) {
		struct texture *t = &textures[i];

		if (t->used && t->resident &&
		    (!oldest || t->last_use < oldest->last_use))
			oldest = t;
		t = &lum_textures[i];
		if (t->used && t->resident &&
		    (!oldest || t->last_use < oldest->last_use))
			oldest = t;
	}
	if (!oldest)
		return 0;
	texture_evict(oldest);
	return 1;
}

static int pages_alloc(int count, uint16_t owner)
{
	int first, i;

	for (;;) {
		first = pages_find(count);
		if (first >= 0)
			break;
		if (!evict_oldest())
			return -1;
	}
	for (i = first; i < first + count; i++)
		hw.page_owner[i] = owner;
	return first;
}

/* --- CLUT slots ----------------------------------------------------------- */

/*
 * A CLUT is a 16x16 block of 32-bit colors: 1 KiB, 4 blocks of 256 bytes.
 * A page of 64x32 pixels holds eight of them, in the places (x, y) =
 * (16 * (k % 4), 16 * (k / 4)), which are the blocks 4 * k to 4 * k + 3 of
 * the page. A slot number is page * 8 + k.
 */
static struct {
	int page;
	uint8_t used;		/* One bit for each slot */
} clut_pages[CLUT_PAGES];

static int clut_alloc(void)
{
	int i, k;

	for (i = 0; i < CLUT_PAGES; i++) {
		if (clut_pages[i].used == 0 || clut_pages[i].used == 0xff)
			continue;
		for (k = 0; k < 8; k++)
			if (!(clut_pages[i].used & (1 << k))) {
				clut_pages[i].used |= 1 << k;
				return 8 * i + k;
			}
	}
	for (i = 0; i < CLUT_PAGES; i++) {
		if (clut_pages[i].used)
			continue;
		clut_pages[i].page = pages_alloc(1, PAGE_CLUT);
		if (clut_pages[i].page < 0)
			return -1;
		clut_pages[i].used = 1;
		return 8 * i;
	}
	return -1;
}

static void clut_free(int slot)
{
	const int i = slot / 8, k = slot % 8;

	if (slot < 0)
		return;
	clut_pages[i].used &= ~(1 << k);
	if (!clut_pages[i].used)
		pages_free(clut_pages[i].page, 1);
}

/*
 * The GS block address of a slot, for TEX0.CBP. The 32 blocks of a 32-bit
 * page are not in a line: the 16x16 region at (16 * (k % 4), 16 * (k / 4))
 * starts at block 0, 4, 16, 20, 8, 12, 24 or 28. (4 * k is wrong for
 * k = 2, 3, 4, 6 and 7, and shows as corrupted colors.)
 */
static unsigned clut_block(int slot)
{
	static const unsigned first[8] = { 0, 4, 16, 20, 8, 12, 24, 28 };

	return (unsigned)clut_pages[slot / 8].page * 32 + first[slot % 8];
}

/* --- OpenGL state --------------------------------------------------------- */

#define MODELVIEW_DEPTH		32
#define PROJECTION_DEPTH	4

struct array {
	const void *pointer;
	int size, stride, enabled;
	GLenum type;
};

static struct {
	GLfloat modelview[MODELVIEW_DEPTH][16];
	GLfloat projection[PROJECTION_DEPTH][16];
	int mv_top, proj_top;
	GLenum matrix_mode;

	uint32_t clip[16];	/* guard * projection * modelview, as float bits */
	uint32_t scale[3], offset[3];
	uint32_t fog_row[4], fog_param[4];
	int final_valid;

	int vp_x, vp_y, vp_w, vp_h;
	GLfloat depth_near, depth_far;
	uint8_t color[4];
	uint32_t texcoord[2];
	uint8_t clear_color[4];
	GLfloat clear_depth;

	int depth_test, depth_write;
	GLenum depth_func;
	int alpha_test;
	GLenum alpha_func;
	GLfloat alpha_ref;
	int blend;
	GLenum blend_src, blend_dst;
	int color_mask;
	int smooth;
	int texture_2d;
	int fog;
	GLenum fog_mode;
	GLfloat fog_density, fog_start, fog_end, fog_color[4];
	unsigned bound;
	GLenum error;

	struct array vertex_array, color_array, texcoord_array;

	/* The open batch */
	const struct texture *batch_texture;
	int in_begin;
	GLenum mode;
	size_t batch_at;	/* Stream index of the first UNPACK code */
	unsigned batch_count;
	int batch_open;		/* A batch is in the stream without its MSCAL yet */
	int batch_list;		/* Its primitives are a list: a later glBegin can add to it */
	unsigned prim_count;	/* Vertices since glBegin */
	int fast;		/* The mode has a fast vertex path (see vertex_fast) */
	/*
	 * The color as the vertex words: VU1 wants 0x80 for 1.0 with a
	 * texture (color_tex), the full byte without (color_flat, with the
	 * alpha halved either way). glColor fills both, so a vertex copies.
	 */
	uint32_t color_tex[4], color_flat[4];
	uint32_t first[VERTEX_WORDS], prev[2][VERTEX_WORDS], quad[4][VERTEX_WORDS];
	size_t fan_base_at, fan_prev_at;	/* Stream index of the fan's base and last corner */
	unsigned header_serial;	/* The serial the header was built at */
	GLenum header_mode;	/* The glBegin mode it was built for */
	unsigned fan_count;	/* Corners of the fan in progress */
	/*
	 * The header of the open batch, and a second buffer for the next
	 * one: batch_open builds the new header there, compares, and the
	 * two swap. No copy of the 384 bytes a batch.
	 */
	uint32_t headers[2][4 * Q_HEADER] __attribute__((aligned(16)));
	uint32_t *header;
} gl;

static const GLfloat identity[16] = {
	1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
};

static void set_error(GLenum e)
{
	if (gl.error == GL_NO_ERROR)
		gl.error = e;
}

static GLfloat *current_matrix(void)
{
	gl.final_valid = 0; state_serial++;
	return gl.matrix_mode == GL_PROJECTION ?
		gl.projection[gl.proj_top] : gl.modelview[gl.mv_top];
}

/* out = a * b. Matrices are column-major, as in OpenGL. */
static void matrix_multiply(GLfloat *out, const GLfloat *a, const GLfloat *b)
{
	GLfloat r[16];
	int c, k;

	for (c = 0; c < 4; c++)
		for (k = 0; k < 4; k++)
			r[4 * c + k] =
				a[k]      * b[4 * c] +
				a[4 + k]  * b[4 * c + 1] +
				a[8 + k]  * b[4 * c + 2] +
				a[12 + k] * b[4 * c + 3];
	memcpy(out, r, sizeof(r));
}

static uint32_t float_bits(GLfloat f)
{
	uint32_t u;

	memcpy(&u, &f, sizeof(u));
	return u;
}

static uint8_t color_byte(GLfloat c);
static int fog_on(void);
static void fog_test_init(void);
static int fog_test;

/* The depth range of XYZF2 is 2^24 with a Z24 buffer, 2^16 with Z16: hw.zscale */
#define ZSCALE hw.zscale

/*
 * VU1 gets a clip matrix and two vectors.
 *
 * The clip matrix gives x, y, z, w with x and y divided by GUARD. VU1
 * compares x, y, z with w to reject triangles. Then it calculates
 * (offset * w + scale * clip) / w, which gives:
 *
 *   x, y: screen pixels, with the bias
 *   z:    depth, larger is nearer, in 0 to ZSCALE
 */
static void update_final(void)
{
	const GLfloat sx = gl.vp_w * 0.5f;
	const GLfloat sy = -gl.vp_h * 0.5f;
	const GLfloat n = gl.depth_near, f = gl.depth_far;
	GLfloat clip[16];
	int c;

	matrix_multiply(clip, gl.projection[gl.proj_top], gl.modelview[gl.mv_top]);
	for (c = 0; c < 4; c++) {
		clip[4 * c + 0] *= 1.0f / GUARD;
		clip[4 * c + 1] *= 1.0f / GUARD;
	}
	for (c = 0; c < 16; c++)
		gl.clip[c] = float_bits(clip[c]);

	gl.scale[0] = float_bits(sx * GUARD);
	gl.scale[1] = float_bits(sy * GUARD);
	gl.offset[0] = float_bits(gl.vp_x + gl.vp_w * 0.5f + BIAS);
	gl.offset[1] = float_bits(hw.height - (gl.vp_y + gl.vp_h * 0.5f) + BIAS);

	/*
	 * Window depth = n + (f - n) * (z + 1) / 2. The GS depth is
	 * (1 - window depth) * ZSCALE, with a margin of 16 at both ends.
	 */
	gl.scale[2] = float_bits(-(ZSCALE - 32.0f) * (f - n) * 0.5f);
	gl.offset[2] = float_bits(16.0f + (ZSCALE - 32.0f) * (1.0f - (n + f) * 0.5f));

	/*
	 * Fog: the distance is -z in eye space, from the third row of the
	 * modelview matrix. Linear fog: f = A * d + B. Exp fog: f = e^(-A * d).
	 * Exp2 fog: f = e^(-(A * d)^2). No fog: f = 1.
	 */
	{
		const GLfloat *m = gl.modelview[gl.mv_top];
		const GLfloat range = gl.fog_end - gl.fog_start;
		GLfloat a = 0.0f, b = 1.0f;

		fog_test_init();
		gl.fog_row[0] = float_bits(-m[2]);
		gl.fog_row[1] = float_bits(-m[6]);
		gl.fog_row[2] = float_bits(-m[10]);
		gl.fog_row[3] = float_bits(-m[14]);
		if (fog_test == 2) {
			a = 0.0f;
			b = 0.5f;
		} else if (fog_test == 3) {
			a = -1.0f / 30.0f;
			b = 1.0f;
		} else if (fog_test >= 4) {
			a = 0.05f;
			b = 0.0f;
		} else if (fog_on() && gl.fog_mode == GL_LINEAR) {
			a = range != 0.0f ? -1.0f / range : 0.0f;
			b = range != 0.0f ? gl.fog_end / range : 1.0f;
		} else if (fog_on()) {
			a = gl.fog_density;
			b = 0.0f;
		}
		gl.fog_param[0] = float_bits(a);
		gl.fog_param[1] = float_bits(b);
		gl.fog_param[2] = float_bits(255.0f);	/* Constants for VU1 */
		gl.fog_param[3] = float_bits(1.0f);
	}

	gl.final_valid = 1;
}

/* --- GS register settings ------------------------------------------------ */

static uint64_t reg_frame(int color_write)
{
	return (uint64_t)hw.frame_page[hw.draw] | ((uint64_t)hw.fbw << 16) |
		((uint64_t)(hw.fb16 ? PSMCT16 : PSMCT32) << 24) |
		(color_write ? 0 : 0xffffffffull << 32);
}

static uint64_t reg_zbuf(int depth_write)
{
	return (uint64_t)hw.zbuf_page | ((uint64_t)(hw.fb16 ? PSMZ16 : PSMZ24) << 24) |
		(depth_write ? 0 : 1ull << 32);
}

static uint64_t reg_scissor(int x0, int y0, int x1, int y1)
{
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > hw.width - 1)
		x1 = hw.width - 1;
	if (y1 > hw.height - 1)
		y1 = hw.height - 1;
	if (x1 < x0)
		x1 = x0;
	if (y1 < y0)
		y1 = y0;
	return (uint64_t)x0 | ((uint64_t)x1 << 16) | ((uint64_t)y0 << 32) |
		((uint64_t)y1 << 48);
}

static uint64_t reg_scissor_viewport(void)
{
	return reg_scissor(gl.vp_x, hw.height - (gl.vp_y + gl.vp_h),
		gl.vp_x + gl.vp_w - 1, hw.height - gl.vp_y - 1);
}

#define REG_XYOFFSET ((uint64_t)(BIAS << 4) | ((uint64_t)(BIAS << 4) << 32))

/* GL depth functions on a GS depth where larger is nearer */
static unsigned gs_ztst(GLenum func)
{
	switch (func) {
	case GL_NEVER:		return 0;
	case GL_LESS:		return 3;	/* GS: greater */
	case GL_LEQUAL:		return 2;	/* GS: greater or equal */
	case GL_GREATER:	return 0;	/* Not possible on the GS */
	case GL_GEQUAL:		return 0;
	case GL_EQUAL:		return 0;
	case GL_NOTEQUAL:	return 1;
	default:		return 1;	/* GL_ALWAYS */
	}
}

static unsigned gs_atst(GLenum func)
{
	switch (func) {
	case GL_NEVER:		return 0;
	case GL_LESS:		return 2;
	case GL_LEQUAL:		return 3;
	case GL_EQUAL:		return 4;
	case GL_GEQUAL:		return 5;
	case GL_GREATER:	return 6;
	case GL_NOTEQUAL:	return 7;
	default:		return 1;	/* GL_ALWAYS */
	}
}

static uint64_t reg_test(void)
{
	uint64_t v = 0;

	if (gl.alpha_test) {
		unsigned ref = (unsigned)(gl.alpha_ref * 128.0f + 0.5f);

		if (ref > 255)
			ref = 255;
		v |= 1 | (gs_atst(gl.alpha_func) << 1) | (ref << 4);
		/* A failed test writes nothing (AFAIL = 0) */
	}
	v |= 1ull << 16;	/* Depth test on. ZTST decides. */
	v |= (uint64_t)(gl.depth_test ? gs_ztst(gl.depth_func) : 1) << 17;
	return v;
}

/*
 * Blend: output = (A - B) * C >> 7 + D. Each of A, B, D is 0 = source,
 * 1 = frame, 2 = zero. C is 0 = source alpha, 2 = the fixed value.
 */
static uint64_t reg_alpha(void)
{
	if (!gl.blend)
		return 0x44;	/* source alpha blend, not used without ABE */
	if (gl.blend_src == GL_SRC_ALPHA && gl.blend_dst == GL_ONE_MINUS_SRC_ALPHA)
		return 0 | (1 << 2) | (0 << 4) | (1 << 6);
	if (gl.blend_src == GL_SRC_ALPHA && gl.blend_dst == GL_ONE)
		return 0 | (2 << 2) | (0 << 4) | (1 << 6);
	if (gl.blend_src == GL_ONE && gl.blend_dst == GL_ONE)
		return 0 | (2 << 2) | (2 << 4) | (1 << 6) | (0x80ull << 32);
	if (gl.blend_src == GL_ONE && gl.blend_dst == GL_ZERO)
		return 0 | (2 << 2) | (2 << 4) | (2 << 6) | (0x80ull << 32);
	if (gl.blend_src == GL_ZERO && gl.blend_dst == GL_SRC_COLOR)
		return 1 | (2 << 2) | (2 << 4) | (2 << 6) | (0x80ull << 32);
	if (gl.blend_src == GL_DST_COLOR && gl.blend_dst == GL_ZERO)
		return 1 | (2 << 2) | (2 << 4) | (2 << 6) | (0x80ull << 32);
	/* Other functions: source alpha blend */
	return 0 | (1 << 2) | (0 << 4) | (1 << 6);
}

/* lum: 1 = the CLUT with the brightness in alpha (PSMT8 only) */
static uint64_t reg_tex0(const struct texture *t, int lum)
{
	uint64_t v = (uint64_t)(t->page * 32) | ((uint64_t)t->tbw << 14) |
		((uint64_t)t->format << 20) | ((uint64_t)t->tw << 26) |
		((uint64_t)t->th << 30) | (1ull << 34) /* RGBA */ |
		(0ull << 35) /* modulate */;

	if (t->format == PSMT8) {
		const int slot = lum ? t->lum_slot : t->clut_slot;

		/* CBP, CPSM = 32 bits, CSM1, CSA 0, CLD 1: load at each TEX0 */
		v |= (uint64_t)clut_block(slot) << 37 | (1ull << 61);
	}
	return v;
}

static uint64_t reg_tex1(const struct texture *t)
{
	/* LCM = 1: fixed level K = 0. MMAG and MMIN: 1 = linear, 0 = nearest */
	return 1 | (t->linear ? (1 << 5) | (1 << 6) : 0);
}

/* --- textures in GS memory ----------------------------------------------- */

/*
 * Send a CLUT to its slot. The GS reads a CLUT in CSM1 order: entry i is at
 * the place (i & 0xe7) | ((i & 0x10) >> 1) | ((i & 0x08) << 1) of the 16x16
 * block, so the entries go out in that order.
 */
static void clut_upload(int slot, const uint32_t *clut)
{
	const int page = clut_pages[slot / 8].page, k = slot % 8;
	uint64_t *q, *tag;
	uint32_t *data;
	int i;

	q = stream_direct_ad(4, 1);
	if (!q)
		return;
	ad(q, GS_BITBLTBUF, ((uint64_t)(page * 32) << 32) | (1ull << 48) |
		((uint64_t)PSMCT32 << 56));
	ad(q + 2, GS_TRXPOS, ((uint64_t)(16 * (k % 4)) << 32) | ((uint64_t)(16 * (k / 4)) << 48));
	ad(q + 4, GS_TRXREG, 16 | (16ull << 32));
	ad(q + 6, GS_TRXDIR, 0);
	batch_close();
	hw.tex_dirty = 1;
	stream_align();
	if (hw.used + 4 + 4 * 65 > hw.limit)
		stream_send(0);
	stream_word(VIF_FLUSH);
	stream_word(VIF_NOP);
	stream_word(VIF_NOP);
	stream_word(VIF_DIRECT(65));
	tag = (uint64_t *)stream_reserve(4);
	data = stream_reserve(4 * 64);
	if (!tag || !data)
		return;
	tag[0] = 64 | (1ull << 15) | (2ull << 58);	/* IMAGE, 64 quadwords */
	tag[1] = 0;
	for (i = 0; i < 256; i++)
		data[(i & 0xe7) | ((i & 0x10) >> 1) | ((i & 0x08) << 1)] = clut[i];
}

/* The second CLUT of a PSMT8 texture: the colors with the brightness in alpha */
static void lum_clut_upload(const struct texture *t)
{
	uint32_t clut[256];
	int i;

	for (i = 0; i < 256; i++) {
		const uint32_t c = t->clut[i];
		unsigned gray = ((c & 0xff) * 77 + ((c >> 8) & 0xff) * 151 + ((c >> 16) & 0xff) * 28) >> 8;

		if (t->lum_slot_scale == 1)
			gray >>= 1;
		clut[i] = (c & 0xffffff) | gray << 24;
	}
	clut_upload(t->lum_slot, clut);
}

/* Send the pixels of a texture to the GS. The texture has its pages. */
/* Send rows y0 to y0 + nrows of the RAM copy to the texture in GS memory. */
static void texture_upload_rows(struct texture *t, int y0, int nrows)
{
	const size_t bpp = t->format == PSMT8 ? 1 : t->format == PSMCT16 ? 2 : 4;
	const size_t row_bytes = (size_t)t->width * bpp;
	const uint8_t *src = t->copy;
	const int end = y0 + nrows;
	int y = y0;

	hw.r_uploads++;
	hw.r_upload_words += row_bytes * nrows / 4;
	batch_close();
	hw.tex_dirty = 1;

	/*
	 * A texture larger than the packet buffer goes in several transfers,
	 * each a whole number of rows with its own BITBLTBUF, TRXPOS and
	 * TRXREG. One transfer must not continue across two sends: the
	 * console then shows the texture corrupted, where the model does not
	 * mind. Each transfer: 8 header quadwords and the rows, and at most
	 * 16384 quadwords of data in one IMAGE packet.
	 */
	while (y < end) {
		size_t rows = (hw.region / 4 - 16) * 16 / row_bytes, n;
		uint64_t *q, *tag;
		uint32_t *data;

		if (rows > (size_t)(end - y))
			rows = end - y;
		if (rows * row_bytes > 16384 * 16)
			rows = 16384 * 16 / row_bytes;
		if (hw.upload_rows && rows > hw.upload_rows)
			rows = hw.upload_rows;	/* PS2GL_UPLOAD_ROWS: a test of the split */
		if (rows < 1)
			rows = 1;	/* A row wider than the buffer cannot happen: 512 x 4 = 2 KiB */
		n = (rows * row_bytes + 15) / 16;

		stream_align();
		if (hw.used + 8 * 4 + 4 * (n + 1) + 8 > hw.limit)
			stream_send(0);
		q = stream_direct_ad(4, 1);
		if (!q)
			return;
		ad(q, GS_BITBLTBUF, ((uint64_t)(t->page * 32) << 32) |
			((uint64_t)t->tbw << 48) | ((uint64_t)t->format << 56));
		ad(q + 2, GS_TRXPOS, (uint64_t)y << 48);
		ad(q + 4, GS_TRXREG, (uint64_t)t->width | ((uint64_t)rows << 32));
		ad(q + 6, GS_TRXDIR, 0);
		stream_align();
		stream_word(VIF_FLUSH);
		stream_word(VIF_NOP);
		stream_word(VIF_NOP);
		stream_word(VIF_DIRECT(n + 1));
		tag = (uint64_t *)stream_reserve(4);
		data = stream_reserve(4 * n);
		if (!tag || !data)
			return;
		tag[0] = (uint64_t)n | (1ull << 15) | (2ull << 58);	/* IMAGE */
		tag[1] = 0;
		memcpy(data, src + (size_t)y * row_bytes, rows * row_bytes);
		if (rows * row_bytes < 16 * n)
			memset((uint8_t *)data + rows * row_bytes, 0, 16 * n - rows * row_bytes);
		y += (int)rows;
	}
}

static void texture_upload(struct texture *t)
{
	texture_upload_rows(t, 0, t->height);
	if (t->format == PSMT8) {
		clut_upload(t->clut_slot, t->clut);
		if (t->lum_slot >= 0)
			lum_clut_upload(t);
	}
	t->resident = 1;
}

/* Make the texture resident. Return 0 if it has no pixels. */
static int texture_bind(struct texture *t)
{
	t->last_use = ++hw.use_counter;
	if (t->resident)
		return 1;
	if (!t->copy)
		return 0;
	t->page = pages_alloc(t->pages, texture_owner(t));
	if (t->page < 0) {
		fprintf(stderr, "ps2gl: no GS memory for a %dx%d texture\n",
			t->width, t->height);
		return 0;
	}
	if (t->format == PSMT8) {
		t->clut_slot = clut_alloc();
		if (t->clut_slot < 0) {
			pages_free(t->page, t->pages);
			fprintf(stderr, "ps2gl: no GS memory for a CLUT\n");
			return 0;
		}
	}
	texture_upload(t);
	return 1;
}

/* The brightness CLUT of a resident PSMT8 texture. Return 0 if none. */
static int lum_slot_get(struct texture *t, int scale)
{
	if (t->lum_slot >= 0 && t->lum_slot_scale == scale)
		return 1;
	if (t->lum_slot < 0) {
		t->lum_slot = clut_alloc();
		if (t->lum_slot < 0)
			return 0;
	}
	t->lum_slot_scale = scale;
	lum_clut_upload(t);
	return 1;
}

/* --- the brightness-in-alpha copy of a texture ---------------------------- */

static void lum_free(struct texture *t)
{
	if (t->lum) {
		texture_evict(t->lum);
		free(t->lum->copy);
		free(t->lum->clut);
		memset(t->lum, 0, sizeof(*t->lum));
		t->lum = NULL;
	}
}

/*
 * Make or update the copy with the brightness in alpha. In the GS, an alpha
 * of 128 is 1.0, so an alpha of gray gives the factor gray / 128: that is
 * the 2x multiply of (GL_DST_COLOR, GL_SRC_COLOR). gray / 2 gives the 1x
 * multiply of (GL_ZERO, GL_SRC_COLOR).
 */
static int tex8_on(void);

/* The brightness of rows y0 to y0 + nrows of t, into its copy l */
static void lum_convert(const struct texture *t, struct texture *l, int y0, int nrows)
{
	const size_t first = (size_t)y0 * t->width, n = (size_t)nrows * t->width;
	size_t i;

	for (i = first; i < first + n; i++) {
		unsigned r, g, b, gray;

		if (t->format == PSMCT16) {
			const uint16_t v = ((const uint16_t *)t->copy)[i];

			r = (v & 31) << 3;
			g = ((v >> 5) & 31) << 3;
			b = ((v >> 10) & 31) << 3;
		} else {
			const uint32_t v = ((const uint32_t *)t->copy)[i];

			r = v & 0xff;
			g = (v >> 8) & 0xff;
			b = (v >> 16) & 0xff;
		}
		gray = (r * 77 + g * 151 + b * 28) >> 8;
		if (l->format == PSMT8) {
			((uint8_t *)l->copy)[i] = gray;	/* The CLUT applies the scale */
		} else {
			if (l->lum_scale == 1)
				gray >>= 1;
			((uint32_t *)l->copy)[i] = r | g << 8 | b << 16 | gray << 24;
		}
	}
}

/* The brightness copy as a 32-bit texture: the colors, and the brightness in alpha */
static struct texture *lum_get32(struct texture *t, struct texture *l, int scale)
{

	free(l->copy);
	free(l->clut);
	l->clut = NULL;
	l->format = PSMCT32;
	l->tbw = t->width < 64 ? 1 : t->width / 64;
	l->pages = l->tbw * ((l->height + 31) / 32);
	l->copy_bytes = (size_t)l->width * l->height * 4;
	if (l->copy_bytes < 16)
		l->copy_bytes = 16;
	l->copy = calloc(1, l->copy_bytes);
	if (!l->copy)
		return NULL;
	l->lum_scale = scale;
	lum_convert(t, l, 0, t->height);
	return l;
}

static struct texture *lum_get(struct texture *t, int scale)
{
	struct texture *l = t->lum;
	size_t i;

	if (!t->copy)
		return NULL;
	if (l && l->lum_scale == scale && l->copy)
		return l;
	if (!l) {
		l = &lum_textures[t - textures];
		texture_init(l);
		l->is_lum = 1;
		t->lum = l;
	}
	texture_evict(l);
	l->width = t->width;
	l->height = t->height;
	l->tw = t->tw;
	l->th = t->th;
	l->linear = t->linear;
	l->clamp = t->clamp;
	if (!tex8_on())
		return lum_get32(t, l, scale);
	l->format = PSMT8;
	l->tbw = t->width < 128 ? 2 : t->width / 64;
	l->pages = (l->tbw / 2) * ((l->height + 63) / 64);
	l->copy_bytes = (size_t)l->width * l->height;
	if (l->copy_bytes < 16)
		l->copy_bytes = 16;
	if (!l->copy)
		l->copy = calloc(1, l->copy_bytes);
	if (!l->clut)
		l->clut = malloc(256 * sizeof(uint32_t));
	if (!l->copy || !l->clut)
		return NULL;
	l->lum_scale = scale;
	l->clut_colors = 256;

	/* The gray ramp: index i is the brightness i, in the alpha channel */
	for (i = 0; i < 256; i++)
		l->clut[i] = (uint32_t)(scale == 1 ? i >> 1 : i) << 24 | 0x808080;

	lum_convert(t, l, 0, t->height);
	return l;
}

/* The multiply blend modes that the GS can do with the alpha trick. */
static int blend_multiply_scale(void)
{
	if (!gl.blend || gl.blend_dst != GL_SRC_COLOR)
		return 0;
	if (gl.blend_src == GL_DST_COLOR)
		return 2;
	if (gl.blend_src == GL_ZERO)
		return 1;
	return 0;
}

/* --- batches ------------------------------------------------------------- */

/* Write the stream codes and data for n quadwords into VU1 memory at addr. */
static void stream_unpack(unsigned addr, const uint32_t *data, unsigned qwords)
{
	while (qwords) {
		const unsigned n = qwords > 256 ? 256 : qwords;
		uint32_t *p;

		stream_word(VIF_UNPACK(n & 0xff, addr));
		p = stream_reserve(4 * n);
		if (!p)
			return;
		memcpy(p, data, 16 * n);
		data += 4 * n;
		addr += n;
		qwords -= n;
	}
}

static unsigned kind_for(GLenum mode)
{
	switch (mode) {
	case GL_POINTS:		return KIND_POINTS;
	case GL_LINES:		return KIND_LINES;
	case GL_LINE_STRIP:
	case GL_LINE_LOOP:	return KIND_LINE_STRIP;
	case GL_TRIANGLE_STRIP:	return KIND_TRIANGLE_STRIP;
	default:		return KIND_FANS;
	}
}

static uint8_t color_byte(GLfloat c);

/* PS2GL_FOG=0 turns fog off for a test, whatever the program asks */
/*
 * PS2GL_FOG for tests, whatever the program asks: 0 is no fog, half is a
 * constant half fog (tests the fog factor path in VU1), linear is linear
 * fog from 0 to 30 units, exp and exp2 are those modes with density 0.05
 * (these test the fog distance).
 */
/* fog_test: 0 none, 1 off, 2 half, 3 linear, 4 exp, 5 exp2 */

static void fog_test_init(void)
{
	static int checked;
	const char *env;

	if (checked)
		return;
	checked = 1;
	env = getenv("PS2GL_FOG");
	if (!env)
		return;
	if (!strcmp(env, "0"))
		fog_test = 1;
	else if (!strcmp(env, "half"))
		fog_test = 2;
	else if (!strcmp(env, "linear"))
		fog_test = 3;
	else if (!strcmp(env, "exp"))
		fog_test = 4;
	else if (!strcmp(env, "exp2"))
		fog_test = 5;
}

static int fog_on(void)
{
	fog_test_init();
	if (fog_test == 1)
		return 0;
	if (fog_test >= 2)
		return 1;
	return gl.fog;
}

static unsigned fog_mode(void)
{
	if (!fog_on())
		return FOG_NONE;
	if (fog_test == 2 || fog_test == 3)
		return FOG_LINEAR;
	if (fog_test == 4)
		return FOG_EXP;
	if (fog_test == 5)
		return FOG_EXP2;
	switch (gl.fog_mode) {
	case GL_LINEAR:	return FOG_LINEAR;
	case GL_EXP2:	return FOG_EXP2;
	default:	return FOG_EXP;
	}
}

/* The fog color register: the GS mixes it in by the fog factor */
static uint64_t reg_fogcol(void)
{
	const uint64_t r = color_byte(gl.fog_color[0]);
	const uint64_t g = color_byte(gl.fog_color[1]);
	const uint64_t b = color_byte(gl.fog_color[2]);

	return r | (g << 8) | (b << 16);
}

/*
 * Open a batch for the primitives that follow. A batch stays open after
 * glEnd: when the next glBegin has the same state (the same header, down
 * to the last register) and a list kind, its primitives go into the same
 * batch. Quake draws each polygon with its own glBegin and glEnd, two
 * thousand in a hall, and a batch costs a VU1 program start and a wait
 * for the previous one.
 */
static void batch_open(void)
{
	struct texture *t;
	int multiply, textured;
	int lum_clut = 0;
	uint32_t *newh, *h;
	uint64_t *q;
	unsigned kind, prim;

	/*
	 * No state change since the header was built, and no texture upload:
	 * the header is the same. The usual case for a game, which draws
	 * thousands of polygons with one glBegin each. Nothing is computed
	 * before this test: Quake takes this path 3,000 times a frame.
	 */
	if (state_serial == gl.header_serial && gl.mode == gl.header_mode &&
	    !hw.tex_dirty && gl.final_valid && !hw.check_header) {
		if (gl.batch_open && gl.batch_list && gl.batch_count < MAX_VERTICES)
			return;
		batch_close();
		batch_start();
		return;
	}

	t = &textures[gl.bound];
	multiply = blend_multiply_scale();
	textured = gl.texture_2d && gl.bound && t->used;
	newh = gl.header == gl.headers[0] ? gl.headers[1] : gl.headers[0];
	h = newh;
	q = (uint64_t *)h;
	kind = kind_for(gl.mode);

	if (textured && multiply && t->format != PSMT8) {
		struct texture *l = lum_get(t, multiply);

		if (l)
			t = l;
	}
	if (textured)
		textured = texture_bind(t);
	if (textured && multiply && t->format == PSMT8 && !t->is_lum)
		lum_clut = lum_slot_get(t, multiply);
	gl.batch_texture = textured ? t : NULL;

	if (!gl.final_valid)
		update_final();

	/* VU1 outputs lists: points, lines or triangles */
	switch (gl.mode) {
	case GL_POINTS:		prim = 0; break;
	case GL_LINES:
	case GL_LINE_STRIP:
	case GL_LINE_LOOP:	prim = 1; break;
	default:		prim = 3; break;
	}
	prim |= (gl.smooth ? 1 << 3 : 0) | (textured ? 1 << 4 : 0) |
		(fog_on() ? 1 << 5 : 0) | (gl.blend ? 1 << 6 : 0) | (1 << 9);

	memset(h, 0, sizeof(gl.headers[0]));
	memcpy(&h[0], gl.clip, sizeof(gl.clip));
	memcpy(&h[4 * Q_SCALE], gl.scale, sizeof(gl.scale));
	memcpy(&h[4 * Q_OFFSET], gl.offset, sizeof(gl.offset));
	/* h[4 * Q_PARAM] = vertex count, set at close */
	h[4 * Q_PARAM + 1] = kind_for(gl.mode);
	h[4 * Q_PARAM + 2] = fog_mode();
	memcpy(&h[4 * Q_FOGROW], gl.fog_row, sizeof(gl.fog_row));
	memcpy(&h[4 * Q_FOGPARAM], gl.fog_param, sizeof(gl.fog_param));

	/* Tag for the register settings: one packet of its own */
	q[2 * Q_PACKET] = (uint64_t)Q_SETTINGS | (1ull << 15) | (1ull << 60);
	q[2 * Q_PACKET + 1] = 0xe;

	q = (uint64_t *)&h[4 * (Q_PACKET + 1)];
	ad(q + 0, GS_FRAME_2, reg_frame(gl.color_mask));
	ad(q + 2, GS_ZBUF_2, reg_zbuf(gl.depth_test && gl.depth_write));
	ad(q + 4, GS_XYOFFSET_2, REG_XYOFFSET);
	ad(q + 6, GS_SCISSOR_2, reg_scissor_viewport());
	ad(q + 8, GS_TEST_2, reg_test());
	if (textured) {
		ad(q + 10, GS_TEX0_2, reg_tex0(t, lum_clut));
		ad(q + 12, GS_TEX1_2, reg_tex1(t));
		ad(q + 14, GS_CLAMP_2, t->clamp ? 1 | (1 << 2) : 0);
	} else {
		ad(q + 10, GS_TEX0_2, 0);
		ad(q + 12, GS_TEX1_2, 1);
		ad(q + 14, GS_CLAMP_2, 0);
	}
	/* The texture cache holds old texels only after an upload */
	ad(q + 16, hw.tex_dirty ? GS_TEXFLUSH : GS_NOP, 0);
	/* Multiply: frame color * alpha. Alpha is the brightness of the copy. */
	if (textured && multiply && (lum_clut || t->is_lum))
		ad(q + 18, GS_ALPHA_2, 1 | (2 << 2) | (0 << 4) | (2 << 6));
	else
		ad(q + 18, GS_ALPHA_2, reg_alpha());
	ad(q + 20, GS_MIPTBP1_2, 0);
	ad(q + 22, GS_MIPTBP2_2, 0);
	ad(q + 24, GS_FOGCOL, reg_fogcol());

	/*
	 * Vertex tag: end of packet, PRIM in the tag, 3 registers for each
	 * vertex: ST, RGBAQ, XYZF2. VU1 puts in the count of each output
	 * packet.
	 */
	q = (uint64_t *)h;
	q[2 * Q_VERTEX_TAG] = (1ull << 15) | (1ull << 46) |
		((uint64_t)prim << 47) | (3ull << 60);
	q[2 * Q_VERTEX_TAG + 1] = 0x412;

	/*
	 * The same state as the open batch, and lists on both sides: go on
	 * with it. The texture upload above may have closed it (an upload
	 * closes the batch before its data), and then it is not open.
	 */
	if (hw.check_header && state_serial == gl.header_serial && gl.mode == gl.header_mode &&
	    !hw.tex_dirty && gl.final_valid && memcmp(newh, gl.header, sizeof(gl.headers[0])) != 0) {
		unsigned k;

		for (k = 0; k < 4 * Q_HEADER; k++)
			if (newh[k] != gl.header[k])
				break;
		fprintf(stderr, "ps2gl check: the header changed with no state change: "
			"word %u (quadword %u) %08x was %08x\n", k, k / 4, newh[k], gl.header[k]);
	}
	/* A header with a TEXFLUSH is for this batch only */
	gl.header_serial = hw.tex_dirty ? state_serial - 1 : state_serial;
	gl.header_mode = gl.mode;
	if (gl.batch_open && gl.batch_list && kind_is_list(kind) &&
	    gl.batch_count < MAX_VERTICES && memcmp(newh, gl.header, sizeof(gl.headers[0])) == 0)
		return;
	batch_close();
	gl.header = newh;
	gl.batch_list = kind_is_list(kind);
	batch_start();
}

/* Start a batch with the header in gl.header */
static void batch_start(void)
{
	/* Room for the largest batch, so that one batch is in one send */
	if (hw.used + 8 + 4 * Q_HEADER + 13 * MAX_VERTICES > hw.limit)
		stream_send(0);

	stream_word(VIF_FLUSH);
	stream_word(VIF_STCYCL);
	gl.batch_at = hw.used;
	stream_unpack(0, gl.header, Q_HEADER);
	gl.batch_count = 0;
	gl.batch_open = 1;
	hw.tex_dirty = 0;
}

/* Close the open batch: the vertex count in its header, then the program start. */
static void batch_close(void)
{
	if (!gl.batch_open)
		return;
	gl.batch_open = 0;
	if (!gl.batch_count) {
		hw.used = gl.batch_at - 2;	/* Remove the empty batch */
		return;
	}

	/* The vertex count in the parameters */
	hw.stream[gl.batch_at + 1 + 4 * Q_PARAM] = gl.batch_count;
	hw.r_batches++;
	stream_word(VIF_MSCAL(VU_LABEL_START));
}

/* Lists merge across glEnd and glBegin. Strips do not. */
static int kind_is_list(unsigned kind)
{
	return kind == KIND_FANS || kind == KIND_TRIANGLES ||
		kind == KIND_POINTS || kind == KIND_LINES;
}

/* Write one vertex to the batch. Returns its stream index, or 0 when full. */
static size_t batch_vertex(const uint32_t *v, uint32_t flag)
{
	const int textured = gl.batch_texture != NULL;
	const unsigned qw = Q_HEADER + 3 * gl.batch_count;
	uint32_t *p;
	unsigned k;

	/*
	 * Each vertex gets its own UNPACK code of 3 quadwords. The code costs
	 * one word for each vertex and needs no block arithmetic.
	 */
	stream_word(VIF_UNPACK(3, qw));
	p = stream_reserve(12);
	if (!p)
		return 0;
	for (k = 0; k < 4; k++)
		p[k] = v[k];
	if (textured) {
		for (k = 4; k < 7; k++)
			p[k] = (v[k] + 1) >> 1;
	} else {
		for (k = 4; k < 7; k++)
			p[k] = v[k];
	}
	p[7] = (v[7] + 1) >> 1;
	p[8] = v[8];
	p[9] = v[9];
	p[10] = v[10];
	p[11] = flag;
	gl.batch_count++;
	hw.r_corners++;
	return p - hw.stream;
}

/* A copy of a vertex at a stream index, as it is, for a fan that goes on in a new batch. */
static size_t batch_copy(size_t at, uint32_t flag)
{
	const uint32_t *v = &hw.stream[at];
	uint32_t *p;
	unsigned k;

	stream_sync();	/* The copy reads what the stores above wrote */
	stream_word(VIF_UNPACK(3, Q_HEADER + 3 * gl.batch_count));
	p = stream_reserve(12);
	if (!p)
		return 0;
	for (k = 0; k < 11; k++)
		p[k] = v[k];
	p[11] = flag;
	gl.batch_count++;
	return p - hw.stream;
}

/*
 * One corner of a fan. Each corner goes out once, with the start flag on
 * the first. Triangles are fans of 3 and quads fans of 4, so these share a
 * batch. A fan that meets the end of a batch goes on in the next one with
 * copies of its base and its last corner.
 */
static void emit_fan(const uint32_t *v, int start)
{
	size_t at;

	if (gl.batch_count == MAX_VERTICES) {
		batch_close();
		batch_open();
		if (!start) {
			const size_t base = batch_copy(gl.fan_base_at, FAN_START);

			if (gl.fan_prev_at != gl.fan_base_at)
				gl.fan_prev_at = batch_copy(gl.fan_prev_at, 0);
			else
				gl.fan_prev_at = base;
			gl.fan_base_at = base;
		}
	}
	at = batch_vertex(v, start ? FAN_START : 0);
	if (start) {
		gl.fan_base_at = at;
		gl.fan_count = 0;
	}
	gl.fan_prev_at = at;
	gl.fan_count++;
}

/* Remove a fan of fewer than 3 corners at glEnd: VU1 needs whole fans. */
static void fan_finish(void)
{
	if (gl.fan_count && gl.fan_count < 3 && gl.batch_count >= gl.fan_count) {
		hw.used -= 13 * gl.fan_count;
		gl.batch_count -= gl.fan_count;
	}
	gl.fan_count = 0;
}

/* Add a vertex. Start a new batch when the VU1 memory is full. */
static void emit(const uint32_t *v)
{
	if (gl.batch_count == MAX_VERTICES) {
		batch_close();
		batch_open();
		/* A strip continues with the vertices it shares. */
		if (gl.mode == GL_TRIANGLE_STRIP) {
			batch_vertex(gl.prev[0], 0);
			batch_vertex(gl.prev[1], 0);
		} else if (gl.mode == GL_LINE_STRIP || gl.mode == GL_LINE_LOOP) {
			batch_vertex(gl.prev[1], 0);
		}
	}
	batch_vertex(v, 0);
}

/* One vertex from glVertex or from an array: s, t, 1, 0, r, g, b, a, x, y, z, 0 */
static void submit(const uint32_t *v)
{
	const size_t size = VERTEX_WORDS * sizeof(uint32_t);

	if (gl.mode == GL_TRIANGLES) {
		emit_fan(v, gl.prim_count % 3 == 0);
	} else if (gl.mode == GL_QUADS) {
		emit_fan(v, (gl.prim_count & 3) == 0);
	} else if (gl.mode == GL_TRIANGLE_FAN || gl.mode == GL_POLYGON) {
		emit_fan(v, gl.prim_count == 0);
	} else if (gl.mode == GL_QUAD_STRIP) {
		/* The quad 0, 1, 3, 2 as a fan */
		const unsigned k = gl.prim_count & 1;

		memcpy(gl.quad[2 + k], v, size);
		if (k == 1 && gl.prim_count >= 3) {
			emit_fan(gl.quad[0], 1);
			emit_fan(gl.quad[1], 0);
			emit_fan(gl.quad[3], 0);
			emit_fan(gl.quad[2], 0);
		}
		if (k == 1) {
			memcpy(gl.quad[0], gl.quad[2], size);
			memcpy(gl.quad[1], gl.quad[3], size);
		}
	} else {
		if (gl.mode == GL_LINE_LOOP && gl.prim_count == 0)
			memcpy(gl.first, v, size);
		emit(v);
		memcpy(gl.prev[0], gl.prev[1], size);
		memcpy(gl.prev[1], v, size);
	}
	gl.prim_count++;
}

/*
 * The position comes as the bits of three floats. With no FPU, a float in
 * a function argument costs a kernel trap, so the internal path moves
 * words only.
 */
/*
 * The fast path of a vertex: a list primitive (a fan of 3 or 4 corners,
 * or a polygon), and the open batch has room for it. The thirteen words
 * go straight into the stream, as batch_vertex writes them, with no copy
 * in between. batch_start made room for a whole batch, so no send can
 * fall in here. The slow path (submit) takes the rest: strips, lines,
 * points, and the corner that fills a batch, which starts the next one.
 */
static void vertex_fast(uint32_t x, uint32_t y, uint32_t z)
{
	const unsigned n = gl.prim_count;
	const uint32_t *c = gl.batch_texture ? gl.color_tex : gl.color_flat;
	uint32_t *p = &hw.stream[hw.used];
	size_t at;
	int start;

	if (gl.mode == GL_TRIANGLES)
		start = n % 3 == 0;
	else if (gl.mode == GL_QUADS)
		start = (n & 3) == 0;
	else
		start = n == 0;

	p[0] = VIF_UNPACK(3, Q_HEADER + 3 * gl.batch_count);
	p[1] = gl.texcoord[0];
	p[2] = gl.texcoord[1];
	p[3] = 0x3f800000;	/* 1.0. VU1 changes it to q = 1 / w. */
	p[4] = 0;
	p[5] = c[0];
	p[6] = c[1];
	p[7] = c[2];
	p[8] = c[3];
	p[9] = x;
	p[10] = y;
	p[11] = z;
	p[12] = start ? FAN_START : 0;
	hw.used += 13;

	at = hw.used - 12;
	if (start) {
		gl.fan_base_at = at;
		gl.fan_count = 0;
	}
	gl.fan_prev_at = at;
	gl.fan_count++;
	gl.batch_count++;
	gl.prim_count = n + 1;
	hw.r_corners++;
}

/* The slow path. A function of its own, so that the fast path has no stack frame. */
static void __attribute__((noinline)) vertex_slow(uint32_t x, uint32_t y, uint32_t z)
{
	uint32_t v[VERTEX_WORDS];

	v[0] = gl.texcoord[0];
	v[1] = gl.texcoord[1];
	v[2] = 0x3f800000;	/* 1.0. VU1 changes it to q = 1 / w. */
	v[3] = 0;
	v[4] = gl.color[0];
	v[5] = gl.color[1];
	v[6] = gl.color[2];
	v[7] = gl.color[3];
	v[8] = x;
	v[9] = y;
	v[10] = z;
	v[11] = 0;
	submit(v);
}

static void vertex_bits(uint32_t x, uint32_t y, uint32_t z)
{
	if (!gl.in_begin)
		return;
	if (gl.fast && gl.batch_open && gl.batch_count < MAX_VERTICES)
		vertex_fast(x, y, z);
	else
		vertex_slow(x, y, z);
}

static void vertex(GLfloat x, GLfloat y, GLfloat z)
{
	vertex_bits(float_bits(x), float_bits(y), float_bits(z));
}

/* --- clear --------------------------------------------------------------- */

static void clear(GLbitfield mask)
{
	const int color = (mask & GL_COLOR_BUFFER_BIT) != 0 && gl.color_mask;
	const int depth = (mask & GL_DEPTH_BUFFER_BIT) != 0;
	const uint32_t rgb = gl.clear_color[0] | gl.clear_color[1] << 8 |
		gl.clear_color[2] << 16;
	GLfloat d = gl.clear_depth;
	uint32_t z;
	uint64_t *q;

	if (!color && !depth)
		return;
	if (gl.in_begin)
		glEnd();

	if (d < 0.0f)
		d = 0.0f;
	if (d > 1.0f)
		d = 1.0f;
	z = (uint32_t)(16.0f + (ZSCALE - 32.0f) * (1.0f - d));

	/* One packet: a sprite fills the viewport. */
	q = stream_direct_ad(9, 1);
	if (!q)
		return;
	ad(q + 0, GS_FRAME_2, reg_frame(color));
	ad(q + 2, GS_ZBUF_2, reg_zbuf(depth));
	ad(q + 4, GS_XYOFFSET_2, REG_XYOFFSET);
	ad(q + 6, GS_SCISSOR_2, reg_scissor_viewport());
	ad(q + 8, GS_TEST_2, (1ull << 16) | (1ull << 17));	/* Always */
	ad(q + 10, GS_PRIM, 6 | (1 << 9));
	ad(q + 12, GS_RGBAQ, (uint64_t)rgb | (0x80ull << 24) |
		(0x3f800000ull << 32));
	ad(q + 14, GS_XYZ2, (uint64_t)(BIAS << 4) |
		((uint64_t)(BIAS << 4) << 16) | ((uint64_t)z << 32));
	ad(q + 16, GS_XYZ2, (uint64_t)((BIAS + hw.width) << 4) |
		((uint64_t)((BIAS + hw.height) << 4) << 16) | ((uint64_t)z << 32));
}

/* --- HUD ----------------------------------------------------------------- */

/*
 * PS2GL_HUD=1 draws three lines in the top left corner of every frame,
 * for any program that uses the library:
 *
 *    14 fps  71 ms gl 22 sw  9 pr 40 gs  7
 *   7890 vtx  41 bat 312KB tex 1840KB  3 ev
 *   cpu  97% mem 14/22 MB swap in 0/s
 *
 * The first line is the report of PS2GL_VERBOSE=2: the frame time and
 * its parts in ms: in the library (gl), in the swap (sw), in the program
 * (pr), and how much of all that waits for the GS (gs). The second is
 * what a frame sent: vertices, batches, kilobytes, texture uploads and
 * evictions. The third comes from /proc: the CPU load, the memory in
 * use, and the pages a second read back from the swap, each of which
 * is a stall of milliseconds on this console. The numbers change once a
 * second. Under them a graph: one bar a frame for the last 128 frames,
 * the height the frame time (32 pixels for 50 ms), a line at 20 ms
 * (50 frames a second). Green under 20 ms, yellow under 40, red above.
 * Averages hide a stutter. The graph shows it.
 *
 * The font is hudfont.h, 96 glyphs of 8x8 in one 128x64 16-bit texture,
 * uploaded once into two GS pages that no texture can take. The text
 * goes to the GS as sprites in a direct packet at the swap: no VU1, no
 * change of the GL state.
 */

#include "hudfont.h"

#define HUD_BARS	128	/* Frames in the graph */
#define HUD_GRAPH_H	32	/* Pixels: 0 to 50 ms */

static struct {
	struct texture font;
	uint16_t pixels[128 * 64];
	char line[3][128];	/* At most 40 columns: 320 pixels */
	float frame_ms[HUD_BARS];	/* The last frames, a ring */
	unsigned frame_at;
	double last_swap;
	unsigned long cpu_busy, cpu_total;
	int cpu_percent;
	long mem_used_mb, mem_total_mb;
	unsigned long swapped_in, swap_rate;	/* Pages a second from the swap: the stalls */
} hud;

static void hud_font_make(void)
{
	struct texture *t = &hud.font;
	int c, y, x;

	for (c = 0; c < 96; c++)
		for (y = 0; y < 8; y++)
			for (x = 0; x < 8; x++)
				hud.pixels[((c / 16) * 8 + y) * 128 + (c % 16) * 8 + x] =
					hud_font[c][y] & (0x80 >> x) ? 0xffff : 0;
	texture_init(t);
	t->width = 128;
	t->height = 64;
	t->tw = 7;
	t->th = 6;
	t->linear = 0;
	texture_size(t, PSMCT16);
	t->copy = hud.pixels;
	t->copy_bytes = sizeof(hud.pixels);
	t->page = -1;
}

/* The CPU load since the last call and the memory in use, from /proc. */
static void hud_system(void)
{
	FILE *f = fopen("/proc/stat", "r");
	unsigned long u, n, sys, idle, io, irq, sirq;
	long total = 0, avail = 0;
	char key[32];
	long value;

	if (f) {
		if (fscanf(f, "cpu %lu %lu %lu %lu %lu %lu %lu", &u, &n, &sys, &idle,
			   &io, &irq, &sirq) == 7) {
			const unsigned long busy = u + n + sys + irq + sirq;
			const unsigned long all = busy + idle + io;

			if (all > hud.cpu_total)
				hud.cpu_percent = (int)(100 * (busy - hud.cpu_busy) /
							(all - hud.cpu_total));
			hud.cpu_busy = busy;
			hud.cpu_total = all;
		}
		fclose(f);
	}
	f = fopen("/proc/meminfo", "r");
	if (f) {
		while (fscanf(f, "%31s %ld kB\n", key, &value) == 2) {
			if (!strcmp(key, "MemTotal:"))
				total = value;
			else if (!strcmp(key, "MemAvailable:"))
				avail = value;
		}
		fclose(f);
		hud.mem_total_mb = total / 1024;
		hud.mem_used_mb = (total - avail) / 1024;
	}
	f = fopen("/proc/vmstat", "r");
	if (f) {
		unsigned long pages = 0;

		while (fscanf(f, "%31s %ld\n", key, &value) == 2)
			if (!strcmp(key, "pswpin"))
				pages = (unsigned long)value;
		fclose(f);
		hud.swap_rate = pages - hud.swapped_in;
		hud.swapped_in = pages;
	}
}

/* The three lines, from the counters of one second. */
static void hud_update(double fps, double frame_ms, double in_gl, double in_swap,
		       double in_send, unsigned long frames)
{
	hud_system();
	snprintf(hud.line[0], sizeof(hud.line[0]),
		 "%3.0f fps %3.0f ms gl %2.0f sw %2.0f pr %2.0f gs %2.0f",
		 fps, frame_ms, in_gl, in_swap, frame_ms - in_gl - in_swap, in_send);
	snprintf(hud.line[1], sizeof(hud.line[1]),
		 "%5lu vtx %3lu bat %3luKB tex %4luKB %2lu ev",
		 hw.r_corners / frames, hw.r_batches / frames,
		 hw.r_words * 4 / 1024 / frames, hw.r_upload_words * 4 / 1024 / frames,
		 hw.r_evictions / frames);
	snprintf(hud.line[2], sizeof(hud.line[2]), "cpu %3d%% mem %ld/%ld MB swap in %lu/s",
		 hud.cpu_percent, hud.mem_used_mb, hud.mem_total_mb, hud.swap_rate);
}

/* Draw the lines on the frame about to show. */
static void hud_draw(void)
{
	const int scale = hw.height >= 400 ? 2 : 1;
	const int cell = 8 * scale;
	const int x0 = 4, y0 = 4;
	const int gy = y0 + 3 * cell + 4;	/* The top of the graph */
	const double now = now_seconds();
	int chars = 0, width = 0, i, row;
	uint64_t *q, *first;

	if (hud.last_swap > 0.0) {
		hud.frame_ms[hud.frame_at % HUD_BARS] = (float)(1000.0 * (now - hud.last_swap));
		hud.frame_at++;
	}
	hud.last_swap = now;
	for (i = 0; i < 3; i++) {
		const int len = (int)strlen(hud.line[i]);

		chars += len;
		if (len > width)
			width = len;
	}
	if (!chars)
		return;
	if (width * cell < HUD_BARS)
		width = (HUD_BARS + cell - 1) / cell;
	if (!hud.font.resident) {
		hud.font.page = pages_alloc(hud.font.pages, PAGE_HUD);
		if (hud.font.page < 0)
			return;
		texture_upload(&hud.font);
	}

	/*
	 * Room for 9 settings, the box (1 + 3), the graph (1 + 3 a bar + 3
	 * for the line), the text (2 + 4 a glyph). stream_direct_end puts
	 * the exact count in the packet.
	 */
	first = q = stream_direct_ad(9 + 4 + 1 + 3 * HUD_BARS + 3 + 2 + 4 * chars, 1);
	if (!q)
		return;
	ad(q + 0, GS_FRAME_2, reg_frame(1));
	ad(q + 2, GS_ZBUF_2, reg_zbuf(0));
	ad(q + 4, GS_XYOFFSET_2, REG_XYOFFSET);
	ad(q + 6, GS_SCISSOR_2, reg_scissor(0, 0, hw.width - 1, hw.height - 1));
	ad(q + 8, GS_TEST_2, (1ull << 16) | (1ull << 17));	/* Depth: always */
	ad(q + 10, GS_TEX0_2, reg_tex0(&hud.font, 0));
	ad(q + 12, GS_TEX1_2, reg_tex1(&hud.font));
	ad(q + 14, GS_CLAMP_2, 0);
	/* (Cs - Cd) * As + Cd */
	ad(q + 16, GS_ALPHA_2, (1ull << 2) | (1ull << 6));
	q += 18;

	/* A dark box behind the text: a sprite with the blend */
	ad(q + 0, GS_PRIM, 6 | (1 << 6) | (1 << 9));
	ad(q + 2, GS_RGBAQ, 0x60ull << 24 | 0x3f800000ull << 32);
	ad(q + 4, GS_XYZ2, (uint64_t)((BIAS + x0 - 2) << 4) |
		((uint64_t)((BIAS + y0 - 2) << 4) << 16));
	ad(q + 6, GS_XYZ2, (uint64_t)((BIAS + x0 + width * cell + 2) << 4) |
		((uint64_t)((BIAS + gy + HUD_GRAPH_H + 2) << 4) << 16));
	q += 8;

	/* The graph: a bar for each frame, the oldest at the left, then the 20 ms line */
	ad(q + 0, GS_PRIM, 6 | (1 << 9));
	q += 2;
	for (i = 0; i < HUD_BARS; i++) {
		const float ms = hud.frame_ms[(hud.frame_at + i) % HUD_BARS];
		const uint32_t rgb = ms < 20.0f ? 0x40ff40 : ms < 40.0f ? 0x40ffff : 0x4040ff;
		const int x = x0 + i;
		int h = (int)(ms * HUD_GRAPH_H / 50.0f);

		if (h > HUD_GRAPH_H)
			h = HUD_GRAPH_H;
		if (h < 1)
			h = 1;
		ad(q + 0, GS_RGBAQ, (uint64_t)rgb | 0x80ull << 24 | 0x3f800000ull << 32);
		ad(q + 2, GS_XYZ2, (uint64_t)((BIAS + x) << 4) |
			((uint64_t)((BIAS + gy + HUD_GRAPH_H - h) << 4) << 16));
		ad(q + 4, GS_XYZ2, (uint64_t)((BIAS + x + 1) << 4) |
			((uint64_t)((BIAS + gy + HUD_GRAPH_H) << 4) << 16));
		q += 6;
	}
	ad(q + 0, GS_RGBAQ, 0xffffffull | 0x80ull << 24 | 0x3f800000ull << 32);
	ad(q + 2, GS_XYZ2, (uint64_t)((BIAS + x0) << 4) |
		((uint64_t)((BIAS + gy + HUD_GRAPH_H - 20 * HUD_GRAPH_H / 50) << 4) << 16));
	ad(q + 4, GS_XYZ2, (uint64_t)((BIAS + x0 + HUD_BARS) << 4) |
		((uint64_t)((BIAS + gy + HUD_GRAPH_H - 20 * HUD_GRAPH_H / 50 + 1) << 4) << 16));
	q += 6;

	/* The glyphs: textured sprites with UV coordinates (FST), white */
	ad(q + 0, GS_PRIM, 6 | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 9));
	ad(q + 2, GS_RGBAQ, 0xffffffull | 0x80ull << 24 | 0x3f800000ull << 32);
	q += 4;
	for (row = 0; row < 3; row++) {
		const char *s = hud.line[row];
		int col;

		for (col = 0; s[col]; col++) {
			const int c = (s[col] >= 32 && s[col] < 127) ? s[col] - 32 : 0;
			const int u = (c % 16) * 8, v = (c / 16) * 8;
			const int x = x0 + col * cell, y = y0 + row * cell;

			ad(q + 0, GS_UV, (uint64_t)(u << 4) | ((uint64_t)(v << 4) << 16));
			ad(q + 2, GS_XYZ2, (uint64_t)((BIAS + x) << 4) |
				((uint64_t)((BIAS + y) << 4) << 16));
			ad(q + 4, GS_UV, (uint64_t)((u + 8) << 4) | ((uint64_t)((v + 8) << 4) << 16));
			ad(q + 6, GS_XYZ2, (uint64_t)((BIAS + x + cell) << 4) |
				((uint64_t)((BIAS + y + cell) << 4) << 16));
			q += 8;
		}
	}
	stream_direct_end(first, q);
}

/* --- display ------------------------------------------------------------- */

static int get_reg(uint32_t reg, uint64_t *value)
{
	struct ps2gs_reg r;

	if (hw.fake)
		return -1;
	r.reg = reg;
	r.pad = 0;
	if (ioctl(hw.fd, PS2GS_IOC_GET_REG, &r) < 0)
		return -1;
	*value = r.value;
	return 0;
}

static void set_reg(uint32_t reg, uint64_t value)
{
	struct ps2gs_reg r;

	dump_reg(reg, value);
	if (hw.fake) {
		if (ps2gl_model_reg)
			ps2gl_model_reg(reg, value);
		return;
	}
	r.reg = reg;
	r.pad = 0;
	r.value = value;
	if (ioctl(hw.fd, PS2GS_IOC_SET_REG, &r) < 0)
		fprintf(stderr, "ps2gl: set register %u: %s\n", reg, strerror(errno));
}

/*
 * Show a frame buffer on read circuit 2. The display window (position and
 * size) comes from the settings of the console. The magnification makes the
 * frame buffer fill that window.
 */
static void show_frame(int index)
{
	uint64_t display = hw.display1;
	unsigned dw, dh, magh, magv;
	const char *env;

	set_reg(PS2GS_REG_DISPFB2, (uint64_t)hw.frame_page[index] |
		((uint64_t)hw.fbw << 9) | ((uint64_t)(hw.fb16 ? PSMCT16 : PSMCT32) << 15));

	if (!hw.have_display1)
		return;

	dw = (unsigned)((display >> 32) & 0xfff) + 1;
	dh = (unsigned)((display >> 44) & 0x7ff) + 1;
	/* The magnification is the nearest whole number. */
	magh = (dw + hw.width / 2) / hw.width;
	magv = (dh + hw.height / 2) / hw.height;
	if (magh < 1)
		magh = 1;
	if (magh > 16)
		magh = 16;
	if (magv < 1)
		magv = 1;
	if (magv > 4)
		magv = 4;
	env = getenv("PS2GL_MAGV");
	if (env && atoi(env) >= 1 && atoi(env) <= 4)
		magv = atoi(env);
	dw = hw.width * magh;
	dh = hw.height * magv;

	display &= 0x7fffffull;		/* Keep DX and DY */
	display |= (uint64_t)(magh - 1) << 23;
	display |= (uint64_t)(magv - 1) << 27;
	display |= (uint64_t)(dw - 1) << 32;
	display |= (uint64_t)(dh - 1) << 44;
	set_reg(PS2GS_REG_DISPLAY2, display);
	/* Circuit 2 on, CRT mode 1, alpha from ALP, ALP = 255 */
	set_reg(PS2GS_REG_PMODE, 0xff66);

	if (hw.verbose) {
		fprintf(stderr, "ps2gl: display %dx%d with %d-bit buffers, magnification %ux%u, "
			"window %ux%u, packet buffer %zu KiB\n", hw.width, hw.height,
			hw.fb16 ? 16 : 32, magh, magv, dw, dh, hw.stream_bytes / 1024);
		hw.verbose = 0;
	}
}

/* --- OpenGL functions: state -------------------------------------------- */

void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
	gl.clear_color[0] = (uint8_t)(r * 255.0f);
	gl.clear_color[1] = (uint8_t)(g * 255.0f);
	gl.clear_color[2] = (uint8_t)(b * 255.0f);
	gl.clear_color[3] = (uint8_t)(a * 255.0f);
}

void glClearDepth(GLclampd depth)
{
	gl.clear_depth = (GLfloat)depth;
}

void glClear(GLbitfield mask)
{
	if (hw.ready)
		clear(mask);
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	gl.vp_x = x;
	gl.vp_y = y;
	gl.vp_w = width;
	gl.vp_h = height;
	gl.final_valid = 0; state_serial++;
}

void glDepthRange(GLclampd znear, GLclampd zfar)
{
	gl.depth_near = (GLfloat)znear;
	gl.depth_far = (GLfloat)zfar;
	gl.final_valid = 0; state_serial++;
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	(void)x;
	(void)y;
	(void)width;
	(void)height;
}

static void set_cap(GLenum cap, int on)
{
	state_serial++;
	switch (cap) {
	case GL_DEPTH_TEST:	gl.depth_test = on; break;
	case GL_TEXTURE_2D:	gl.texture_2d = on; break;
	case GL_ALPHA_TEST:	gl.alpha_test = on; break;
	case GL_BLEND:		gl.blend = on; break;
	case GL_FOG:		gl.fog = on; gl.final_valid = 0; state_serial++; break;
	default: break;
	}
}

void glEnable(GLenum cap)
{
	set_cap(cap, 1);
}

void glDisable(GLenum cap)
{
	set_cap(cap, 0);
}

GLboolean glIsEnabled(GLenum cap)
{
	switch (cap) {
	case GL_DEPTH_TEST:	return gl.depth_test;
	case GL_TEXTURE_2D:	return gl.texture_2d;
	case GL_ALPHA_TEST:	return gl.alpha_test;
	case GL_BLEND:		return gl.blend;
	case GL_FOG:		return gl.fog;
	default:		return GL_FALSE;
	}
}

void glShadeModel(GLenum mode)
{
	state_serial++;
	gl.smooth = mode == GL_SMOOTH;
}

void glDepthFunc(GLenum func)
{
	state_serial++;
	gl.depth_func = func;
}

void glDepthMask(GLboolean flag)
{
	state_serial++;
	gl.depth_write = flag != GL_FALSE;
}

void glAlphaFunc(GLenum func, GLclampf ref)
{
	state_serial++;
	gl.alpha_func = func;
	gl.alpha_ref = ref;
}

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
	state_serial++;
	gl.blend_src = sfactor;
	gl.blend_dst = dfactor;
}

void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
	state_serial++;
	(void)a;
	gl.color_mask = r || g || b;
}

void glCullFace(GLenum mode)
{
	(void)mode;
}

void glFrontFace(GLenum mode)
{
	(void)mode;
}

void glPolygonMode(GLenum face, GLenum mode)
{
	(void)face;
	(void)mode;
}

void glPolygonOffset(GLfloat factor, GLfloat units)
{
	(void)factor;
	(void)units;
}

void glHint(GLenum target, GLenum mode)
{
	(void)target;
	(void)mode;
}

/* One frame buffer to draw and to read: the back buffer */
void glDrawBuffer(GLenum mode)
{
	(void)mode;
}

void glReadBuffer(GLenum mode)
{
	(void)mode;
}

void glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type)
{
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	(void)type;
}

void glWindowPos2i(GLint x, GLint y)
{
	(void)x;
	(void)y;
}

/* Texture priorities: the library keeps the textures with the latest use */
void glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
	(void)n;
	(void)textures;
	(void)priorities;
}

GLboolean glAreTexturesResident(GLsizei n, const GLuint *names, GLboolean *residences)
{
	GLboolean all = GL_TRUE;
	GLsizei i;

	for (i = 0; i < n; i++) {
		residences[i] = names[i] && names[i] <= MAX_TEXTURES &&
			textures[names[i]].resident ? GL_TRUE : GL_FALSE;
		if (!residences[i])
			all = GL_FALSE;
	}
	return all;
}

void glLineWidth(GLfloat width)
{
	(void)width;
}

void glPointSize(GLfloat size)
{
	(void)size;
}

void glFogf(GLenum pname, GLfloat param)
{
	switch (pname) {
	case GL_FOG_MODE:	gl.fog_mode = (GLenum)param; break;
	case GL_FOG_DENSITY:	gl.fog_density = param; break;
	case GL_FOG_START:	gl.fog_start = param; break;
	case GL_FOG_END:	gl.fog_end = param; break;
	default: break;
	}
	gl.final_valid = 0; state_serial++;
}

void glFogi(GLenum pname, GLint param)
{
	glFogf(pname, (GLfloat)param);
}

void glFogfv(GLenum pname, const GLfloat *params)
{
	if (pname == GL_FOG_COLOR)
		memcpy(gl.fog_color, params, sizeof(gl.fog_color));
	else
		glFogf(pname, params[0]);
}

void glFogiv(GLenum pname, const GLint *params)
{
	glFogi(pname, params[0]);
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
	state_serial++;
	(void)target;
	(void)pname;
	(void)param;
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
	state_serial++;
	(void)target;
	(void)pname;
	(void)param;
}

void glPushAttrib(GLbitfield mask)
{
	(void)mask;
}

void glPopAttrib(void)
{
	state_serial++;
}

GLenum glGetError(void)
{
	GLenum e = gl.error;

	gl.error = GL_NO_ERROR;
	return e;
}

const GLubyte *glGetString(GLenum name)
{
	switch (name) {
	case GL_VENDOR:		return (const GLubyte *)"meta-playstation2";
	case GL_RENDERER:	return (const GLubyte *)"PlayStation 2 GS and VU1";
	case GL_VERSION:	return (const GLubyte *)"1.1 ps2gl";
	case GL_EXTENSIONS:	return (const GLubyte *)"GL_EXT_bgra GL_EXT_texture_edge_clamp GL_SGIS_texture_edge_clamp";
	default:		return (const GLubyte *)"";
	}
}

void glGetIntegerv(GLenum pname, GLint *params)
{
	switch (pname) {
	case GL_MAX_TEXTURE_SIZE:	params[0] = MAX_TEXTURE_SIZE; break;
	case GL_VIEWPORT:
		params[0] = gl.vp_x;
		params[1] = gl.vp_y;
		params[2] = gl.vp_w;
		params[3] = gl.vp_h;
		break;
	case GL_DOUBLEBUFFER:		params[0] = 1; break;
	case GL_RED_BITS:
	case GL_GREEN_BITS:
	case GL_BLUE_BITS:
	case GL_ALPHA_BITS:		params[0] = 8; break;
	case GL_DEPTH_BITS:		params[0] = 24; break;
	case GL_STENCIL_BITS:		params[0] = 0; break;
	case GL_MATRIX_MODE:		params[0] = gl.matrix_mode; break;
	case GL_TEXTURE_BINDING_2D:	params[0] = gl.bound; break;
	case GL_MAX_MODELVIEW_STACK_DEPTH: params[0] = MODELVIEW_DEPTH; break;
	case GL_MAX_PROJECTION_STACK_DEPTH: params[0] = PROJECTION_DEPTH; break;
	default:			params[0] = 0; break;
	}
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
	GLint i[4];

	switch (pname) {
	case GL_MODELVIEW_MATRIX:
		memcpy(params, gl.modelview[gl.mv_top], sizeof(identity));
		break;
	case GL_PROJECTION_MATRIX:
		memcpy(params, gl.projection[gl.proj_top], sizeof(identity));
		break;
	case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
		params[0] = 1.0f;
		break;
	case GL_VIEWPORT:
		glGetIntegerv(pname, i);
		params[0] = i[0];
		params[1] = i[1];
		params[2] = i[2];
		params[3] = i[3];
		break;
	default:
		glGetIntegerv(pname, i);
		params[0] = (GLfloat)i[0];
		break;
	}
}

void glGetDoublev(GLenum pname, GLdouble *params)
{
	GLfloat f[16];
	int n = pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX ? 16 :
		pname == GL_VIEWPORT ? 4 : 1;
	int k;

	glGetFloatv(pname, f);
	for (k = 0; k < n; k++)
		params[k] = f[k];
}

void glGetBooleanv(GLenum pname, GLboolean *params)
{
	GLint i[4];

	glGetIntegerv(pname, i);
	params[0] = i[0] != 0;
}

/*
 * Read the frame buffer. The GS sends the rectangle to the packet buffer
 * (a local to host transfer, PS2GS_IOC_RECEIVE), in strips of rows that
 * fit in one half of the buffer. GL counts rows from the bottom, the GS
 * from the top, so the strips go in from the last row. The pixels come in
 * the frame format, 16 or 32 bits, and become GL_RGB, GL_RGBA, GL_BGR or
 * GL_BGRA bytes.
 */
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
	GLenum format, GLenum type, GLvoid *pixels)
{
	const int out_bytes = (format == GL_RGB || format == GL_BGR) ? 3 :
		(format == GL_ALPHA || format == GL_LUMINANCE) ? 1 : 4;
	const int bgr = format == GL_BGR || format == GL_BGRA;
	const int bpp = hw.fb16 ? 2 : 4;
	const int fb_format = hw.fb16 ? PSMCT16 : PSMCT32;
	uint8_t *dst = pixels;
	int rows_max, y0;

	if (!pixels || width <= 0 || height <= 0 || type != GL_UNSIGNED_BYTE)
		return;
	if (x < 0 || y < 0 || x + width > hw.width || y + height > hw.height) {
		set_error(GL_INVALID_VALUE);
		return;
	}
	if (gl.in_begin)
		glEnd();
	if (!hw.fake && hw.driver_version < 2) {
		memset(pixels, 0, (size_t)width * height * out_bytes);
		return;
	}

	rows_max = (int)(hw.region * 4 / ((size_t)width * bpp));
	if (rows_max < 1)
		rows_max = 1;
	for (y0 = 0; y0 < height; y0 += rows_max) {
		const int rows = y0 + rows_max > height ? height - y0 : rows_max;
		/* GL row y0 + rows - 1 (the top of this strip) is GS row sy */
		const int sy = hw.height - (y + y0 + rows);
		const size_t bytes = (size_t)width * rows * bpp;
		const size_t qwc = (bytes + 15) / 16;
		uint64_t *q;
		const uint8_t *src;
		int r, c;

		q = stream_direct_ad(4, 1);
		if (!q)
			return;
		ad(q, GS_BITBLTBUF, (uint64_t)(hw.frame_page[hw.draw] * 32) |
			((uint64_t)hw.fbw << 16) | ((uint64_t)fb_format << 24));
		ad(q + 2, GS_TRXPOS, (uint64_t)x | ((uint64_t)sy << 16));
		ad(q + 4, GS_TRXREG, (uint64_t)width | ((uint64_t)rows << 32));
		ad(q + 6, GS_TRXDIR, 1);
		stream_send(1);

		/* Both halves are free after a send with a wait: use the current one */
		if (hw.fake) {
			if (ps2gl_model_receive)
				ps2gl_model_receive(hw.stream + hw.base, qwc * 4);
		} else {
			struct ps2gs_receive rcv = { hw.base * 4, qwc };

			if (ioctl(hw.fd, PS2GS_IOC_RECEIVE, &rcv) < 0) {
				fprintf(stderr, "ps2gl: receive: %s\n", strerror(errno));
				return;
			}
		}
		src = (const uint8_t *)(hw.stream + hw.base);
		for (r = 0; r < rows; r++) {
			/* GS row r of the strip is GL row y0 + rows - 1 - r */
			uint8_t *d = dst + ((size_t)(y0 + rows - 1 - r) * width) * out_bytes;
			const uint8_t *p = src + (size_t)r * width * bpp;

			for (c = 0; c < width; c++, p += bpp, d += out_bytes) {
				unsigned red, green, blue, alpha;

				if (hw.fb16) {
					const unsigned v = p[0] | p[1] << 8;

					red = (v & 31) << 3;
					green = ((v >> 5) & 31) << 3;
					blue = ((v >> 10) & 31) << 3;
					alpha = v & 0x8000 ? 255 : 0;
				} else {
					red = p[0];
					green = p[1];
					blue = p[2];
					alpha = p[3] >= 128 ? 255 : p[3] * 2;
				}
				if (out_bytes == 1) {
					d[0] = format == GL_ALPHA ? alpha : (red * 77 + green * 151 + blue * 28) >> 8;
					continue;
				}
				d[0] = bgr ? blue : red;
				d[1] = green;
				d[2] = bgr ? red : blue;
				if (out_bytes == 4)
					d[3] = alpha;
			}
		}
	}
}

void glPixelStorei(GLenum pname, GLint param)
{
	(void)pname;
	(void)param;
}

/* --- OpenGL functions: matrices ----------------------------------------- */

void glMatrixMode(GLenum mode)
{
	gl.matrix_mode = mode;
}

void glLoadIdentity(void)
{
	memcpy(current_matrix(), identity, sizeof(identity));
}

void glLoadMatrixf(const GLfloat *m)
{
	memcpy(current_matrix(), m, 16 * sizeof(GLfloat));
}

void glLoadMatrixd(const GLdouble *m)
{
	GLfloat *c = current_matrix();
	int k;

	for (k = 0; k < 16; k++)
		c[k] = (GLfloat)m[k];
}

void glMultMatrixf(const GLfloat *m)
{
	GLfloat *c = current_matrix();

	matrix_multiply(c, c, m);
}

void glMultMatrixd(const GLdouble *m)
{
	GLfloat f[16];
	int k;

	for (k = 0; k < 16; k++)
		f[k] = (GLfloat)m[k];
	glMultMatrixf(f);
}

void glPushMatrix(void)
{
	if (gl.matrix_mode == GL_PROJECTION) {
		if (gl.proj_top + 1 < PROJECTION_DEPTH) {
			memcpy(gl.projection[gl.proj_top + 1],
				gl.projection[gl.proj_top], sizeof(identity));
			gl.proj_top++;
		} else {
			set_error(GL_STACK_OVERFLOW);
		}
	} else if (gl.mv_top + 1 < MODELVIEW_DEPTH) {
		memcpy(gl.modelview[gl.mv_top + 1],
			gl.modelview[gl.mv_top], sizeof(identity));
		gl.mv_top++;
	} else {
		set_error(GL_STACK_OVERFLOW);
	}
}

void glPopMatrix(void)
{
	if (gl.matrix_mode == GL_PROJECTION) {
		if (gl.proj_top > 0)
			gl.proj_top--;
		else
			set_error(GL_STACK_UNDERFLOW);
	} else if (gl.mv_top > 0) {
		gl.mv_top--;
	} else {
		set_error(GL_STACK_UNDERFLOW);
	}
	gl.final_valid = 0; state_serial++;
}

/* Translate and scale change few values, so they do not use a full multiply. */
void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat *m = current_matrix();
	int k;

	for (k = 0; k < 4; k++)
		m[12 + k] += m[k] * x + m[4 + k] * y + m[8 + k] * z;
}

void glTranslated(GLdouble x, GLdouble y, GLdouble z)
{
	glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat *m = current_matrix();
	int k;

	for (k = 0; k < 4; k++) {
		m[k] *= x;
		m[4 + k] *= y;
		m[8 + k] *= z;
	}
}

void glScaled(GLdouble x, GLdouble y, GLdouble z)
{
	glScalef((GLfloat)x, (GLfloat)y, (GLfloat)z);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	const GLfloat rad = angle * (3.14159265f / 180.0f);
	const GLfloat c = cosf(rad), s = sinf(rad), t = 1.0f - c;
	GLfloat len = sqrtf(x * x + y * y + z * z);
	GLfloat r[16];

	if (len == 0.0f)
		return;
	x /= len;
	y /= len;
	z /= len;

	r[0] = t * x * x + c;      r[4] = t * x * y - s * z;  r[8]  = t * x * z + s * y;  r[12] = 0;
	r[1] = t * x * y + s * z;  r[5] = t * y * y + c;      r[9]  = t * y * z - s * x;  r[13] = 0;
	r[2] = t * x * z - s * y;  r[6] = t * y * z + s * x;  r[10] = t * z * z + c;      r[14] = 0;
	r[3] = 0;                  r[7] = 0;                  r[11] = 0;                  r[15] = 1;

	glMultMatrixf(r);
}

void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
	glRotatef((GLfloat)angle, (GLfloat)x, (GLfloat)y, (GLfloat)z);
}

void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
	GLdouble znear, GLdouble zfar)
{
	const GLfloat l = left, r = right, b = bottom, t = top;
	const GLfloat n = znear, f = zfar;
	GLfloat m[16] = { 0 };

	m[0] = 2 * n / (r - l);
	m[5] = 2 * n / (t - b);
	m[8] = (r + l) / (r - l);
	m[9] = (t + b) / (t - b);
	m[10] = -(f + n) / (f - n);
	m[11] = -1;
	m[14] = -2 * f * n / (f - n);

	glMultMatrixf(m);
}

void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
	GLdouble znear, GLdouble zfar)
{
	const GLfloat l = left, r = right, b = bottom, t = top;
	const GLfloat n = znear, f = zfar;
	GLfloat m[16] = { 0 };

	m[0] = 2 / (r - l);
	m[5] = 2 / (t - b);
	m[10] = -2 / (f - n);
	m[12] = -(r + l) / (r - l);
	m[13] = -(t + b) / (t - b);
	m[14] = -(f + n) / (f - n);
	m[15] = 1;

	glMultMatrixf(m);
}

void ps2glPerspective(GLfloat fovy_degrees, GLfloat aspect,
	GLfloat znear, GLfloat zfar)
{
	const GLfloat top = znear * tanf(fovy_degrees * (3.14159265f / 360.0f));
	const GLfloat right = top * aspect;

	glFrustum(-right, right, -top, top, znear, zfar);
}

/* --- OpenGL functions: vertices ----------------------------------------- */

static double now_seconds(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

void glBegin(GLenum mode)
{
	if (!hw.ready || gl.in_begin)
		return;
	/*
	 * The time inside glBegin..glEnd: 1 pair in 32, chosen at random,
	 * times 32. A clock read is a system call here, 7.5 us, and Quake
	 * has 3,600 pairs a frame: 1 in 8 cost 7 ms a frame.
	 *
	 * A sample holds the cost of one clock read. With the cost measured
	 * at the start, the HUD showed about 30 ms a frame more in gl than
	 * the frame had, and a negative pr: in a game a read costs more,
	 * most likely because the kernel's code and data leave the 8 KiB
	 * data cache between two reads. So the sample takes two reads here,
	 * and their difference is the cost of a read at that moment.
	 */
	if (hw.report) {
		hw.r_sample = hw.r_sample * 1103515245u + 12345u;
		hw.r_sampled = ((hw.r_sample >> 16) & 31) == 0;
		if (hw.r_sampled) {
			const double t = now_seconds();

			hw.r_t0 = now_seconds();
			hw.r_read = hw.r_t0 - t;
		}
	}
	gl.in_begin = 1;
	gl.mode = mode;
	gl.prim_count = 0;
	gl.fan_count = 0;
	gl.fast = mode == GL_TRIANGLES || mode == GL_QUADS ||
		mode == GL_POLYGON || mode == GL_TRIANGLE_FAN;
	batch_open();
}

void glEnd(void)
{
	if (!gl.in_begin)
		return;
	if (gl.mode == GL_LINE_LOOP && gl.prim_count >= 2)
		emit(gl.first);
	if (gl.batch_list)
		fan_finish();
	if (!gl.batch_list)
		batch_close();
	gl.in_begin = 0;
	if (hw.report && hw.r_sampled) {
		const double dt = now_seconds() - hw.r_t0 - hw.r_read;

		hw.r_in_gl += 32.0 * (dt > 0.0 ? dt : 0.0);
	}
}

void glVertex2f(GLfloat x, GLfloat y)		{ vertex(x, y, 0.0f); }
void glVertex2i(GLint x, GLint y)		{ vertex((GLfloat)x, (GLfloat)y, 0.0f); }
void glVertex2d(GLdouble x, GLdouble y)		{ vertex((GLfloat)x, (GLfloat)y, 0.0f); }
void glVertex2fv(const GLfloat *v)
{
	const uint32_t *b = (const uint32_t *)v;

	vertex_bits(b[0], b[1], 0);
}
void glVertex3f(GLfloat x, GLfloat y, GLfloat z) { vertex(x, y, z); }
void glVertex3i(GLint x, GLint y, GLint z)	{ vertex((GLfloat)x, (GLfloat)y, (GLfloat)z); }
void glVertex3d(GLdouble x, GLdouble y, GLdouble z) { vertex((GLfloat)x, (GLfloat)y, (GLfloat)z); }
void glVertex3fv(const GLfloat *v)
{
	const uint32_t *b = (const uint32_t *)v;

	vertex_bits(b[0], b[1], b[2]);
}
void glVertex3dv(const GLdouble *v)		{ vertex((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2]); }
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
	if (w != 0.0f && w != 1.0f)
		vertex(x / w, y / w, z / w);
	else
		vertex(x, y, z);
}

void glTexCoord2f(GLfloat s, GLfloat t)
{
	gl.texcoord[0] = float_bits(s);
	gl.texcoord[1] = float_bits(t);
}

void glTexCoord2d(GLdouble s, GLdouble t)	{ glTexCoord2f((GLfloat)s, (GLfloat)t); }
void glTexCoord2i(GLint s, GLint t)		{ glTexCoord2f((GLfloat)s, (GLfloat)t); }
void glTexCoord2fv(const GLfloat *v)
{
	const uint32_t *b = (const uint32_t *)v;

	gl.texcoord[0] = b[0];
	gl.texcoord[1] = b[1];
}

static uint8_t color_byte(GLfloat c)
{
	return c <= 0.0f ? 0 : c >= 1.0f ? 255 : (uint8_t)(c * 255.0f);
}

void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
	gl.color[0] = r;
	gl.color[1] = g;
	gl.color[2] = b;
	gl.color[3] = a;
	gl.color_flat[0] = r;
	gl.color_flat[1] = g;
	gl.color_flat[2] = b;
	gl.color_flat[3] = (a + 1u) >> 1;
	gl.color_tex[0] = (r + 1u) >> 1;
	gl.color_tex[1] = (g + 1u) >> 1;
	gl.color_tex[2] = (b + 1u) >> 1;
	gl.color_tex[3] = gl.color_flat[3];
}

void glColor3ub(GLubyte r, GLubyte g, GLubyte b)	{ glColor4ub(r, g, b, 255); }
void glColor4ubv(const GLubyte *v)			{ glColor4ub(v[0], v[1], v[2], v[3]); }
void glColor3ubv(const GLubyte *v)			{ glColor4ub(v[0], v[1], v[2], 255); }

void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	glColor4ub(color_byte(r), color_byte(g), color_byte(b), color_byte(a));
}

void glColor3f(GLfloat r, GLfloat g, GLfloat b)	{ glColor4f(r, g, b, 1.0f); }
void glColor4fv(const GLfloat *v)		{ glColor4f(v[0], v[1], v[2], v[3]); }
void glColor3fv(const GLfloat *v)		{ glColor4f(v[0], v[1], v[2], 1.0f); }
void glColor4d(GLdouble r, GLdouble g, GLdouble b, GLdouble a)
{
	glColor4f((GLfloat)r, (GLfloat)g, (GLfloat)b, (GLfloat)a);
}
void glColor3d(GLdouble r, GLdouble g, GLdouble b)
{
	glColor4f((GLfloat)r, (GLfloat)g, (GLfloat)b, 1.0f);
}

void glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
	(void)x;
	(void)y;
	(void)z;
}

void glNormal3fv(const GLfloat *v)
{
	(void)v;
}

/* --- vertex arrays -------------------------------------------------------- */

static struct array *client_array(GLenum array)
{
	switch (array) {
	case GL_VERTEX_ARRAY:		return &gl.vertex_array;
	case GL_COLOR_ARRAY:		return &gl.color_array;
	case GL_TEXTURE_COORD_ARRAY:	return &gl.texcoord_array;
	default:			return NULL;
	}
}

void glEnableClientState(GLenum array)
{
	struct array *a = client_array(array);

	if (a)
		a->enabled = 1;
}

void glDisableClientState(GLenum array)
{
	struct array *a = client_array(array);

	if (a)
		a->enabled = 0;
}

static int type_size(GLenum type)
{
	switch (type) {
	case GL_UNSIGNED_BYTE:
	case GL_BYTE:		return 1;
	case GL_SHORT:
	case GL_UNSIGNED_SHORT:	return 2;
	case GL_DOUBLE:		return 8;
	default:		return 4;
	}
}

static void set_array(struct array *a, GLint size, GLenum type, GLsizei stride,
	const GLvoid *pointer)
{
	a->pointer = pointer;
	a->size = size;
	a->type = type;
	a->stride = stride ? stride : size * type_size(type);
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	set_array(&gl.vertex_array, size, type, stride, pointer);
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	set_array(&gl.texcoord_array, size, type, stride, pointer);
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	set_array(&gl.color_array, size, type, stride, pointer);
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
	(void)type;
	(void)stride;
	(void)pointer;
}

static GLfloat array_float(const struct array *a, const void *p, int k)
{
	switch (a->type) {
	case GL_FLOAT:		return ((const GLfloat *)p)[k];
	case GL_DOUBLE:		return (GLfloat)((const GLdouble *)p)[k];
	case GL_SHORT:		return ((const GLshort *)p)[k];
	case GL_INT:		return ((const GLint *)p)[k];
	default:		return 0.0f;
	}
}

static void array_vertex(GLint i)
{
	const struct array *va = &gl.vertex_array;
	const struct array *ca = &gl.color_array;
	const struct array *ta = &gl.texcoord_array;
	const void *p;

	if (ta->enabled && ta->pointer) {
		p = (const char *)ta->pointer + i * ta->stride;
		if (ta->type == GL_FLOAT) {
			const uint32_t *t = p;

			gl.texcoord[0] = t[0];
			gl.texcoord[1] = t[1];
		} else {
			glTexCoord2f(array_float(ta, p, 0), array_float(ta, p, 1));
		}
	}
	if (ca->enabled && ca->pointer) {
		p = (const char *)ca->pointer + i * ca->stride;
		if (ca->type == GL_UNSIGNED_BYTE) {
			const uint8_t *c = p;

			glColor4ub(c[0], c[1], c[2], ca->size >= 4 ? c[3] : 255);
		} else {
			glColor4f(array_float(ca, p, 0), array_float(ca, p, 1),
				array_float(ca, p, 2),
				ca->size >= 4 ? array_float(ca, p, 3) : 1.0f);
		}
	}
	p = (const char *)va->pointer + i * va->stride;
	if (va->type == GL_FLOAT) {
		const uint32_t *v = p;

		vertex_bits(v[0], v[1], va->size >= 3 ? v[2] : 0);
	} else {
		vertex(array_float(va, p, 0), array_float(va, p, 1),
			va->size >= 3 ? array_float(va, p, 2) : 0.0f);
	}
}

void glArrayElement(GLint i)
{
	if (gl.in_begin && gl.vertex_array.enabled && gl.vertex_array.pointer)
		array_vertex(i);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	GLsizei i;

	if (!gl.vertex_array.enabled || !gl.vertex_array.pointer || gl.in_begin)
		return;
	glBegin(mode);
	for (i = first; i < first + count; i++)
		array_vertex(i);
	glEnd();
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	GLsizei i;

	if (!gl.vertex_array.enabled || !gl.vertex_array.pointer || gl.in_begin)
		return;
	glBegin(mode);
	for (i = 0; i < count; i++) {
		GLint k;

		switch (type) {
		case GL_UNSIGNED_BYTE:	k = ((const GLubyte *)indices)[i]; break;
		case GL_UNSIGNED_SHORT:	k = ((const GLushort *)indices)[i]; break;
		default:		k = ((const GLuint *)indices)[i]; break;
		}
		array_vertex(k);
	}
	glEnd();
}

void glFlush(void)
{
}

void glFinish(void)
{
	if (!hw.ready)
		return;
	if (gl.in_begin)
		glEnd();
	stream_send(1);
	if (!hw.fake)
		ioctl(hw.fd, PS2GS_IOC_WAIT, PS2GS_WAIT_GIF | PS2GS_WAIT_VU1);
}

/* --- OpenGL functions: textures ----------------------------------------- */

static void texture_free(struct texture *t)
{
	lum_free(t);
	texture_evict(t);
	free(t->copy);
	free(t->clut);
	memset(t, 0, sizeof(*t));
	t->clut_slot = -1;
	t->lum_slot = -1;
}

void glGenTextures(GLsizei n, GLuint *names)
{
	GLsizei i;
	unsigned id = 1;

	for (i = 0; i < n; i++) {
		while (id <= MAX_TEXTURES && textures[id].used)
			id++;
		if (id > MAX_TEXTURES) {
			names[i] = 0;
			set_error(GL_OUT_OF_MEMORY);
			continue;
		}
		texture_init(&textures[id]);
		names[i] = id;
	}
}

void glDeleteTextures(GLsizei n, const GLuint *names)
{
	state_serial++;
	GLsizei i;

	for (i = 0; i < n; i++) {
		if (names[i] >= 1 && names[i] <= MAX_TEXTURES) {
			texture_free(&textures[names[i]]);
			if (gl.bound == names[i])
				gl.bound = 0;
		}
	}
}

GLboolean glIsTexture(GLuint name)
{
	return name >= 1 && name <= MAX_TEXTURES && textures[name].used;
}

void glBindTexture(GLenum target, GLuint name)
{
	(void)target;
	if (name > MAX_TEXTURES)
		return;
	/* The same texture again changes nothing: the batch header stays */
	if (name == gl.bound)
		return;
	state_serial++;
	if (name && !textures[name].used) {
		/* A name from the program, not from glGenTextures */
		texture_init(&textures[name]);
	}
	gl.bound = name;
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
	state_serial++;
	struct texture *t = &textures[gl.bound];

	(void)target;
	if (!gl.bound)
		return;
	if (pname == GL_TEXTURE_MAG_FILTER || pname == GL_TEXTURE_MIN_FILTER)
		t->linear = param != GL_NEAREST &&
			param != GL_NEAREST_MIPMAP_NEAREST &&
			param != GL_NEAREST_MIPMAP_LINEAR;
	else if (pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T)
		t->clamp = param != GL_REPEAT;
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
	state_serial++;
	glTexParameteri(target, pname, (GLint)param);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
	state_serial++;
	glTexParameteri(target, pname, params[0]);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
	state_serial++;
	glTexParameteri(target, pname, (GLint)params[0]);
}

static int log2_exact(int n)
{
	int k;

	for (k = 0; k <= 10; k++)
		if (n == 1 << k)
			return k;
	return -1;
}

/*
 * Read a row of w source pixels as r, g, b, a bytes. The format switch is
 * outside the loop: a texture load or a lightmap update converts
 * thousands of pixels, and a call with a switch for each of them was the
 * largest cost of a Quake frame in a lit room.
 */
static void read_row(GLenum format, GLenum type, const uint8_t *p, uint8_t *out, int w)
{
	int x;

	switch (format) {
	case GL_RGB:
		for (x = 0; x < w; x++, p += 3, out += 4) {
			out[0] = p[0];
			out[1] = p[1];
			out[2] = p[2];
			out[3] = 255;
		}
		break;
	case GL_BGR:
		for (x = 0; x < w; x++, p += 3, out += 4) {
			out[0] = p[2];
			out[1] = p[1];
			out[2] = p[0];
			out[3] = 255;
		}
		break;
	case GL_BGRA:
		for (x = 0; x < w; x++, p += 4, out += 4) {
			out[0] = p[2];
			out[1] = p[1];
			out[2] = p[0];
			out[3] = p[3];
		}
		break;
	case GL_ALPHA:
		for (x = 0; x < w; x++, p += 1, out += 4) {
			out[0] = 255;
			out[1] = 255;
			out[2] = 255;
			out[3] = p[0];
		}
		break;
	case GL_LUMINANCE:
		for (x = 0; x < w; x++, p += 1, out += 4) {
			out[0] = p[0];
			out[1] = p[0];
			out[2] = p[0];
			out[3] = 255;
		}
		break;
	case GL_LUMINANCE_ALPHA:
		for (x = 0; x < w; x++, p += 2, out += 4) {
			out[0] = p[0];
			out[1] = p[0];
			out[2] = p[0];
			out[3] = p[1];
		}
		break;
	default:	/* GL_RGBA */
		if (type == GL_UNSIGNED_SHORT_5_5_5_1) {
			for (x = 0; x < w; x++, p += 2, out += 4) {
				const unsigned v = p[0] | p[1] << 8;

				out[0] = (v >> 11) << 3;
				out[1] = ((v >> 6) & 31) << 3;
				out[2] = ((v >> 1) & 31) << 3;
				out[3] = (v & 1) ? 255 : 0;
			}
		} else {
			memcpy(out, p, (size_t)w * 4);
		}
		break;
	}
}

/* A row of r, g, b, a bytes, for read_row: the widest texture is 1024 */
static uint8_t row_pixels[1024 * 4];

static int source_bytes(GLenum format, GLenum type)
{
	if (type == GL_UNSIGNED_SHORT_5_5_5_1 || type == GL_UNSIGNED_SHORT_5_6_5)
		return 2;
	switch (format) {
	case GL_RGB:
	case GL_BGR:		return 3;
	case GL_ALPHA:
	case GL_LUMINANCE:	return 1;
	case GL_LUMINANCE_ALPHA: return 2;
	default:		return 4;
	}
}

/* --- colors to indices ------------------------------------------------------ */

/*
 * A hash map from GS 32-bit colors to CLUT indices, for one texture at a
 * time. 1024 places for at most 256 colors: the search is short.
 */
static struct {
	uint32_t key[1024];
	uint8_t index[1024], used[1024];
} cmap;

static void cmap_reset(const uint32_t *clut, int colors)
{
	int i;

	memset(cmap.used, 0, sizeof(cmap.used));
	for (i = 0; i < colors; i++) {
		unsigned h = (clut[i] * 2654435761u) >> 22;

		while (cmap.used[h] && cmap.key[h] != clut[i])
			h = (h + 1) & 1023;
		if (!cmap.used[h]) {
			cmap.used[h] = 1;
			cmap.key[h] = clut[i];
			cmap.index[h] = (uint8_t)i;
		}
	}
}

/* The index of a color: -1 if the CLUT does not have it */
static int cmap_find(uint32_t color)
{
	unsigned h = (color * 2654435761u) >> 22;

	while (cmap.used[h]) {
		if (cmap.key[h] == color)
			return cmap.index[h];
		h = (h + 1) & 1023;
	}
	return -1;
}

static void cmap_add(uint32_t color, int index)
{
	unsigned h = (color * 2654435761u) >> 22;

	while (cmap.used[h])
		h = (h + 1) & 1023;
	cmap.used[h] = 1;
	cmap.key[h] = color;
	cmap.index[h] = (uint8_t)index;
}

static uint32_t gs_color(const uint8_t c[4])
{
	return c[0] | c[1] << 8 | c[2] << 16 | (uint32_t)((c[3] + 1) >> 1) << 24;
}

/* A value gs_color never returns: its alpha byte is at most 128 */
#define NO_COLOR	0xffffffffu

/*
 * 8-bit textures: a texture with at most 256 colors becomes PSMT8 indices
 * with a CLUT. Half the RAM for the copy, half the bytes in an upload,
 * twice the textures in GS memory. On by default: the console has
 * 32 MiB, and the copies of a game's textures at 2 bytes a pixel push a
 * game into the swap. PS2GL_TEX8=0 turns them off.
 */
static int tex8_on(void)
{
	static int checked, on;

	if (!checked) {
		const char *env = getenv("PS2GL_TEX8");

		on = !env || atoi(env) != 0;
		checked = 1;
	}
	return on;
}

/*
 * Build the CLUT of an image. Return the number of colors, or -1 when the
 * image has more than 256.
 */
static int palettize(GLsizei width, GLsizei height, GLenum format,
	GLenum type, const void *pixels, uint32_t *clut)
{
	const int sb = source_bytes(format, type);
	const uint8_t *src = pixels;
	uint32_t last = NO_COLOR;
	int x, y, colors = 0;

	if (!tex8_on() || !pixels || !hw.tex16)
		return -1;
	cmap_reset(clut, 0);
	for (y = 0; y < height; y++, src += (size_t)sb * width) {
		read_row(format, type, src, row_pixels, width);
		for (x = 0; x < width; x++) {
			const uint32_t color = gs_color(row_pixels + 4 * x);

			if (color == last || cmap_find(color) >= 0) {
				last = color;
				continue;
			}
			if (colors == 256)
				return -1;
			clut[colors] = color;
			cmap_add(color, colors);
			colors++;
			last = color;
		}
	}
	return colors;
}

static void texture_size(struct texture *t, int format)
{
	t->format = format;
	if (format == PSMT8) {
		/* A page is 128 x 64 indices, and the buffer is at least 128 wide */
		t->tbw = t->width < 128 ? 2 : t->width / 64;
		t->pages = (t->tbw / 2) * ((t->height + 63) / 64);
		t->copy_bytes = (size_t)t->width * t->height;
	} else {
		const int page_h = format == PSMCT16 ? 64 : 32;

		t->tbw = t->width < 64 ? 1 : t->width / 64;
		/* A page is 64 x 32 pixels with 32 bits, 64 x 64 with 16 bits */
		t->pages = t->tbw * ((t->height + page_h - 1) / page_h);
		t->copy_bytes = (size_t)t->width * t->height * (format == PSMCT16 ? 2 : 4);
	}
	if (t->copy_bytes < 16)
		t->copy_bytes = 16;
}

/* A PSMT8 texture gets colors that its CLUT cannot hold: make it 16 or 32 bits */
static int depalettize(struct texture *t)
{
	const size_t n = (size_t)t->width * t->height;
	const uint8_t *src = t->copy;
	int format = PSMCT16, i;
	void *copy;
	size_t k;

	for (i = 0; i < t->clut_colors; i++) {
		const unsigned a = t->clut[i] >> 24;

		if (a > 8 && a < 120)
			format = PSMCT32;
	}
	texture_evict(t);
	texture_size(t, format);
	copy = calloc(1, t->copy_bytes);
	if (!copy)
		return 0;
	for (k = 0; k < n; k++) {
		const uint32_t c = t->clut[src[k]];

		if (format == PSMCT16)
			((uint16_t *)copy)[k] = ((c & 0xff) >> 3) | (((c >> 8) & 0xff) >> 3) << 5 |
				(((c >> 16) & 0xff) >> 3) << 10 | ((c >> 24) >= 64 ? 0x8000 : 0);
		else
			((uint32_t *)copy)[k] = c;
	}
	free(t->copy);
	t->copy = copy;
	free(t->clut);
	t->clut = NULL;
	t->clut_colors = 0;
	return 1;
}

/*
 * Write a rectangle of source pixels into the RAM copy of the texture. A
 * row at a time: read_row makes r, g, b, a bytes, then one loop for the
 * format of the copy. For an 8-bit texture a run of one color, usual in a
 * lightmap, costs one compare a pixel. Returns 0 when the CLUT ran out of
 * room: the texture is 16 or 32 bits then, evicted, with the rectangle
 * written.
 */
static int texture_write(struct texture *t, int x0, int y0, int w, int h,
	GLenum format, GLenum type, const void *pixels)
{
	const int sb = source_bytes(format, type);
	const uint8_t *src = pixels;
	uint32_t last_color = NO_COLOR, last_index = 0;
	int x, y;

	if (t->format == PSMT8)
		cmap_reset(t->clut, t->clut_colors);
	for (y = 0; y < h; y++, src += (size_t)sb * w) {
		const size_t at = (size_t)(y0 + y) * t->width + x0;

		read_row(format, type, src, row_pixels, w);
		if (t->format == PSMT8) {
			uint8_t *dst = (uint8_t *)t->copy + at;

			for (x = 0; x < w; x++) {
				const uint32_t color = gs_color(row_pixels + 4 * x);
				int index;

				if (color == last_color) {
					dst[x] = (uint8_t)last_index;
					continue;
				}
				index = cmap_find(color);
				if (index < 0 && t->clut_colors < 256) {
					index = t->clut_colors++;
					t->clut[index] = color;
					cmap_add(color, index);
				}
				if (index < 0) {
					/* No room in the CLUT: start again with real colors */
					if (!depalettize(t))
						return 0;
					texture_write(t, x0, y0, w, h, format, type, pixels);
					return 0;
				}
				dst[x] = (uint8_t)index;
				last_color = color;
				last_index = (uint32_t)index;
			}
		} else if (t->format == PSMCT16) {
			uint16_t *dst = (uint16_t *)t->copy + at;

			for (x = 0; x < w; x++) {
				const uint8_t *c = row_pixels + 4 * x;

				dst[x] = (c[0] >> 3) | ((c[1] >> 3) << 5) |
					((c[2] >> 3) << 10) | (c[3] >= 128 ? 0x8000 : 0);
			}
		} else {
			uint32_t *dst = (uint32_t *)t->copy + at;

			for (x = 0; x < w; x++)
				dst[x] = gs_color(row_pixels + 4 * x);
		}
	}
	return 1;
}

/*
 * 16-bit textures have one bit of alpha. A texture with alpha values between
 * the two ends needs 32 bits, or the blending of a translucent picture is
 * wrong. Only the formats with an alpha channel get the check.
 */
static int wanted_format(GLsizei width, GLsizei height, GLenum format,
	GLenum type, const void *pixels)
{
	const uint8_t *p = pixels;
	size_t i, n = (size_t)width * height;
	int sb, aoff;

	if (!hw.tex16)
		return PSMCT32;
	if (!pixels || type != GL_UNSIGNED_BYTE)
		return PSMCT16;
	switch (format) {
	case GL_RGBA:
	case GL_BGRA:		sb = 4; aoff = 3; break;
	case GL_ALPHA:		sb = 1; aoff = 0; break;
	case GL_LUMINANCE_ALPHA: sb = 2; aoff = 1; break;
	default:		return PSMCT16;
	}
	for (i = 0; i < n; i++) {
		const uint8_t a = p[i * sb + aoff];

		if (a > 16 && a < 240)
			return PSMCT32;
	}
	return PSMCT16;
}

/*
 * Only level 0. GL_RGBA, GL_BGRA, GL_RGB, GL_BGR, GL_ALPHA, GL_LUMINANCE with
 * GL_UNSIGNED_BYTE, and GL_RGBA with GL_UNSIGNED_SHORT_5_5_5_1. Width and
 * height must be powers of 2 up to 1024.
 */
void glTexImage2D(GLenum target, GLint level, GLint internalformat,
	GLsizei width, GLsizei height, GLint border, GLenum format,
	GLenum type, const GLvoid *pixels)
{
	state_serial++;
	struct texture *t = &textures[gl.bound];
	const int tw = log2_exact(width), th = log2_exact(height);
	uint32_t clut[256];
	int colors, wanted;

	(void)target;
	(void)internalformat;
	(void)border;

	if (!gl.bound || !t->used || level != 0)
		return;
	if (tw < 0 || th < 0 || tw > 10 || th > 10) {
		set_error(GL_INVALID_VALUE);
		return;
	}

	lum_free(t);
	/* With at most 256 colors the texture is 8-bit indices and a CLUT */
	colors = palettize(width, height, format, type, pixels, clut);
	wanted = colors >= 0 ? PSMT8 : wanted_format(width, height, format, type, pixels);
	if (t->width != width || t->height != height || !t->copy || t->format != wanted) {
		texture_evict(t);
		free(t->copy);
		t->width = width;
		t->height = height;
		t->tw = tw;
		t->th = th;
		texture_size(t, wanted);
		t->copy = calloc(1, t->copy_bytes);
		if (!t->copy) {
			set_error(GL_OUT_OF_MEMORY);
			free(t->clut);
			texture_init(t);
			return;
		}
	} else {
		texture_evict(t);
	}
	if (wanted == PSMT8) {
		if (!t->clut)
			t->clut = malloc(256 * sizeof(uint32_t));
		if (!t->clut) {
			set_error(GL_OUT_OF_MEMORY);
			return;
		}
		memcpy(t->clut, clut, sizeof(clut));
		t->clut_colors = colors;
	}

	if (pixels)
		texture_write(t, 0, 0, width, height, format, type, pixels);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
	GLsizei width, GLsizei height, GLenum format, GLenum type,
	const GLvoid *pixels)
{
	state_serial++;
	struct texture *t = &textures[gl.bound];

	(void)target;
	if (!gl.bound || !t->used || level != 0 || !t->copy || !pixels)
		return;
	if (xoffset < 0 || yoffset < 0 || xoffset + width > t->width ||
	    yoffset + height > t->height) {
		set_error(GL_INVALID_VALUE);
		return;
	}
	if (t->format == PSMT8) {
		/*
		 * In place, with the CLUT sent again when it grew. A lightmap
		 * starts dark with few colors, so it is 8-bit, and its updates
		 * add colors. When the CLUT runs out of room the texture becomes
		 * 16 or 32 bits and is evicted: it uploads whole at the next
		 * bind, once.
		 */
		const int colors = t->clut_colors;

		if (!texture_write(t, xoffset, yoffset, width, height, format, type, pixels))
			return;
		if (t->resident) {
			texture_upload_rows(t, yoffset, height);
			if (t->clut_colors != colors) {
				clut_upload(t->clut_slot, t->clut);
				if (t->lum_slot >= 0)
					lum_clut_upload(t);
			}
		}
		return;
	}

	/*
	 * In place: the RAM copy, then only the changed rows of the texture in
	 * GS memory and of its brightness copy. A game updates its lightmaps
	 * this way many times a frame, and a full upload each time was the
	 * largest cost in a lit room.
	 */
	texture_write(t, xoffset, yoffset, width, height, format, type, pixels);
	if (t->resident)
		texture_upload_rows(t, yoffset, height);
	if (t->lum && t->lum->copy) {
		lum_convert(t, t->lum, yoffset, height);
		if (t->lum->resident)
			texture_upload_rows(t->lum, yoffset, height);
	}
}

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params)
{
	const struct texture *t = &textures[gl.bound];

	(void)target;
	if (!gl.bound || level != 0) {
		params[0] = 0;
		return;
	}
	switch (pname) {
	case GL_TEXTURE_WIDTH:		params[0] = t->width; break;
	case GL_TEXTURE_HEIGHT:		params[0] = t->height; break;
	case GL_TEXTURE_INTERNAL_FORMAT: params[0] = GL_RGBA; break;
	default:			params[0] = 0; break;
	}
}

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
	const struct texture *t = &textures[gl.bound];

	(void)target;
	switch (pname) {
	case GL_TEXTURE_MAG_FILTER:
	case GL_TEXTURE_MIN_FILTER:
		params[0] = t->linear ? GL_LINEAR : GL_NEAREST;
		break;
	case GL_TEXTURE_WRAP_S:
	case GL_TEXTURE_WRAP_T:
		params[0] = t->clamp ? GL_CLAMP : GL_REPEAT;
		break;
	default:
		params[0] = 0.0f;
		break;
	}
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params)
{
	GLfloat f;

	glGetTexParameterfv(target, pname, &f);
	params[0] = (GLint)f;
}

/* No stencil buffer. The state is accepted and has no effect. */
void glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
	(void)func;
	(void)ref;
	(void)mask;
}

void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
	(void)fail;
	(void)zfail;
	(void)zpass;
}

void glStencilMask(GLuint mask)
{
	(void)mask;
}

void glClearStencil(GLint s)
{
	(void)s;
}

void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type,
	GLvoid *pixels)
{
	(void)target;
	(void)level;
	(void)format;
	(void)type;
	(void)pixels;
}

/*
 * Copy a part of the frame into the bound texture: a local-to-local
 * transfer of the GS, frame pages to texture pages, one row at a time
 * because the GL frame has its first row at the bottom and the GS frame
 * at the top. The texture takes the format of the frame. Its copy in RAM
 * is not the truth any more: after an eviction the texture comes back
 * with its old pixels, and the program copies again (QuakeSpasm does it
 * at each frame, for its water).
 */
void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset,
	GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
	state_serial++;
	struct texture *t = &textures[gl.bound];
	const int format = hw.fb16 ? PSMCT16 : PSMCT32;
	uint64_t *q;
	int r;

	(void)target;
	if (!gl.bound || !t->used || level != 0 || !t->copy)
		return;
	/* Clip to the texture and to the frame */
	if (xoffset < 0) {
		width += xoffset;
		x -= xoffset;
		xoffset = 0;
	}
	if (yoffset < 0) {
		height += yoffset;
		y -= yoffset;
		yoffset = 0;
	}
	if (x < 0) {
		width += x;
		xoffset -= x;
		x = 0;
	}
	if (y < 0) {
		height += y;
		yoffset -= y;
		y = 0;
	}
	if (xoffset + width > t->width)
		width = t->width - xoffset;
	if (yoffset + height > t->height)
		height = t->height - yoffset;
	if (x + width > hw.width)
		width = hw.width - x;
	if (y + height > hw.height)
		height = hw.height - y;
	if (width <= 0 || height <= 0)
		return;

	lum_free(t);
	if (t->format != format) {
		texture_evict(t);
		free(t->copy);
		free(t->clut);
		t->clut = NULL;
		texture_size(t, format);
		t->copy = calloc(1, t->copy_bytes);
		if (!t->copy) {
			set_error(GL_OUT_OF_MEMORY);
			texture_init(t);
			return;
		}
	}
	if (!texture_bind(t))
		return;

	/* All drawing before this point is in the stream: FLUSH, then the copy */
	hw.tex_dirty = 1;
	q = stream_direct_ad(2 + 2 * height, 1);
	if (!q)
		return;
	ad(q, GS_BITBLTBUF, (uint64_t)(hw.frame_page[hw.draw] * 32) | ((uint64_t)hw.fbw << 16) |
		((uint64_t)format << 24) | ((uint64_t)(t->page * 32) << 32) |
		((uint64_t)t->tbw << 48) | ((uint64_t)format << 56));
	ad(q + 2, GS_TRXREG, (uint64_t)width | (1ull << 32));
	for (r = 0; r < height; r++) {
		/* GL row y + r from the bottom is GS row height - 1 - (y + r) */
		const unsigned sy = hw.height - 1 - (y + r), dy = yoffset + r;

		ad(q + 4 + 4 * r, GS_TRXPOS, (uint64_t)x | ((uint64_t)sy << 16) |
			((uint64_t)xoffset << 32) | ((uint64_t)dy << 48));
		ad(q + 6 + 4 * r, GS_TRXDIR, 2);
	}
	/* The texture cache may hold the old pixels */
	q = stream_direct_ad(1, 1);
	if (q)
		ad(q, GS_TEXFLUSH, 0);
}

/* --- keyboard ------------------------------------------------------------- */

/* The kernel input event for a 32-bit system */
struct key_event {
	uint32_t sec;
	uint32_t usec;
	uint16_t type;
	uint16_t code;
	int32_t value;
};

#define GRAB_EVENTS _IOW('E', 0x90, int)

static void open_keyboards(void)
{
	char path[32];
	int i;

	for (i = 0; i < 16 && hw.keyboards < 8; i++) {
		int fd;

		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		ioctl(fd, GRAB_EVENTS, 1);
		hw.keyboard[hw.keyboards++] = fd;
	}
}

static void close_keyboards(void)
{
	int i;

	for (i = 0; i < hw.keyboards; i++) {
		ioctl(hw.keyboard[i], GRAB_EVENTS, 0);
		close(hw.keyboard[i]);
	}
	hw.keyboards = 0;
}

int ps2glGetKey(int *code, int *pressed)
{
	struct key_event e;
	int i;

	for (i = 0; i < hw.keyboards; i++) {
		while (read(hw.keyboard[i], &e, sizeof(e)) == (ssize_t)sizeof(e)) {
			if (e.type == 1 && e.value != 2) {	/* EV_KEY, no repeat */
				*code = e.code;
				*pressed = e.value;
				return 1;
			}
		}
	}
	return 0;
}

/* --- context -------------------------------------------------------------- */

int ps2glWidth(void)
{
	return hw.width;
}

int ps2glHeight(void)
{
	return hw.height;
}

static void state_defaults(void)
{
	int i;

	memset(&gl, 0, sizeof(gl));
	for (i = 0; i < MODELVIEW_DEPTH; i++)
		memcpy(gl.modelview[i], identity, sizeof(identity));
	for (i = 0; i < PROJECTION_DEPTH; i++)
		memcpy(gl.projection[i], identity, sizeof(identity));
	gl.matrix_mode = GL_MODELVIEW;
	gl.vp_w = hw.width;
	gl.vp_h = hw.height;
	gl.depth_near = 0.0f;
	gl.depth_far = 1.0f;
	gl.clear_depth = 1.0f;
	gl.header = gl.headers[0];
	glColor4ub(255, 255, 255, 255);
	gl.depth_write = 1;
	gl.depth_func = GL_LESS;
	gl.alpha_func = GL_ALWAYS;
	gl.blend_src = GL_ONE;
	gl.blend_dst = GL_ZERO;
	gl.color_mask = 1;
	gl.smooth = 1;
	gl.fog_mode = GL_EXP;
	gl.fog_density = 1.0f;
	gl.fog_end = 1.0f;
	gl.error = GL_NO_ERROR;
}

/* GS settings that stay for the whole context, and the VU1 program. */
static void init_stream(void)
{
	uint64_t *q;
	uint32_t *p;
	unsigned i;

	q = stream_direct_ad(7, 1);
	if (!q)
		return;
	ad(q + 0, GS_PRMODECONT, 1);	/* PRIM holds the attributes */
	ad(q + 2, GS_TEXA, 0x80ull << 32);	/* 16-bit alpha: 0 or 0x80 */
	ad(q + 4, GS_COLCLAMP, 1);
	ad(q + 6, GS_DTHE, 0);
	ad(q + 8, GS_PABE, 0);
	ad(q + 10, GS_FBA_2, 0);
	ad(q + 12, GS_DIMX, 0);

	/* The VU1 program, in pieces of at most 256 instructions, once VU1 is idle */
	stream_align();
	stream_word(VIF_FLUSHE);
	for (i = 0; i < VU_PROGRAM_WORDS / 2; i += 256) {
		const unsigned n = VU_PROGRAM_WORDS / 2 - i > 256 ? 256 : VU_PROGRAM_WORDS / 2 - i;

		stream_align();
		stream_word(VIF_NOP);
		stream_word(VIF_NOP);
		stream_word(VIF_NOP);
		stream_word(VIF_MPG(n & 0xff, i));
		p = stream_reserve(2 * n);
		if (!p)
			return;
		memcpy(p, &vu_program_words[2 * i], 8 * n);
	}

	/* The clip planes: inside is where plane . (x, y, z, w) >= 0 */
	{
		static const float planes[6][4] = {
			{ -1, 0, 0, 1 }, { 1, 0, 0, 1 },
			{ 0, -1, 0, 1 }, { 0, 1, 0, 1 },
			{ 0, 0, -1, 1 }, { 0, 0, 1, 1 },
		};
		uint32_t words[24];

		memcpy(words, planes, sizeof(words));
		stream_word(VIF_STCYCL);
		stream_unpack(Q_PLANES, words, 6);
	}
	stream_align();
	stream_send(1);
}

static int context_open(int width, int height, int keyboard)
{
	struct ps2gs_info info;
	const char *env;
	int frame_pages, i;

	if (hw.ready)
		return 0;
	memset(&hw, 0, sizeof(hw));
	hw.fd = -1;
	hw.tty = -1;

	if (width < 64 || width > 640 || height < 64 || height > 512) {
		fprintf(stderr, "ps2gl: %dx%d is not a possible size\n", width, height);
		return -1;
	}

	env = getenv("PS2GL_FAKE");
	hw.fake = env && atoi(env) == 1;
	if (hw.fake) {
		hw.stream = mmap(NULL, STREAM_BYTES, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	} else {
		hw.fd = open("/dev/ps2gs", O_RDWR);
		if (hw.fd < 0) {
			fprintf(stderr, "ps2gl: cannot open /dev/ps2gs: %s\n", strerror(errno));
			return -1;
		}
		if (ioctl(hw.fd, PS2GS_IOC_INFO, &info) < 0) {
			fprintf(stderr, "ps2gl: /dev/ps2gs: %s\n", strerror(errno));
			close(hw.fd);
			return -1;
		}
		hw.driver_version = info.version;
		/*
		 * The kernel needs the buffer in one piece of physical memory.
		 * After a program filled its memory, a large piece can be
		 * missing, so try smaller sizes. A small buffer means more
		 * sends for each frame, not a failure.
		 */
		/*
		 * 256 KiB by default: the kernel pins the buffer, and a larger
		 * one is hard to find in one piece on a 32 MiB console.
		 * PS2GL_BUFFER gives another size in KiB, up to 1024.
		 */
		hw.stream_bytes = 256 * 1024;
		env = getenv("PS2GL_BUFFER");
		if (env && atoi(env) >= 64 && atoi(env) <= 1024)
			hw.stream_bytes = (size_t)atoi(env) * 1024;
		for (;;) {
			hw.stream = mmap(NULL, hw.stream_bytes, PROT_READ | PROT_WRITE,
				MAP_SHARED, hw.fd, 0);
			if (hw.stream != MAP_FAILED || hw.stream_bytes <= 64 * 1024)
				break;
			hw.stream_bytes /= 2;
		}
	}
	if (hw.stream == MAP_FAILED) {
		fprintf(stderr, "ps2gl: mmap: %s\n", strerror(errno));
		if (hw.fd >= 0)
			close(hw.fd);
		return -1;
	}
	if (hw.fake) {
		hw.stream_bytes = STREAM_BYTES;
		env = getenv("PS2GL_BUFFER");	/* The same choice as on the console */
		if (env && atoi(env) >= 64 && atoi(env) <= 1024)
			hw.stream_bytes = (size_t)atoi(env) * 1024;
		if (ps2gl_model_init)
			ps2gl_model_init(width, height);
	}
	hw.capacity = hw.stream_bytes / 4;
	hw.region = hw.capacity / 2;
	hw.base = 0;
	hw.limit = hw.region;
	hw.used = 0;
	{
		/*
		 * The clock's own cost: a glBegin..glEnd sample of the report
		 * has one call inside it, and Quake makes thousands of pairs
		 * a frame.
		 */
		const double t0 = now_seconds();
		int i;

		for (i = 0; i < 200; i++)
			hw.clock_cost = now_seconds();
		hw.clock_cost = (hw.clock_cost - t0) / 200;
	}
	if (getenv("PS2GL_VERBOSE")) {
		/* The write speed of the packet buffer against ordinary memory */
		void *ram = malloc(hw.stream_bytes);
		double t0, t1, t2;

		if (ram) {
			memset(ram, 0, hw.stream_bytes);
			t0 = now_seconds();
			memset(hw.stream, 0, hw.stream_bytes);
			__sync_synchronize();
			t1 = now_seconds();
			memset(ram, 1, hw.stream_bytes);
			t2 = now_seconds();
			if (((volatile uint8_t *)ram)[hw.stream_bytes - 1] != 1)
				t2 = t1;	/* Keeps the compiler from dropping the memset */
			fprintf(stderr, "ps2gl: writing %zu KiB: packet buffer %.0f MB/s, RAM %.0f MB/s. "
				"A clock read: %.0f ns\n",
				hw.stream_bytes / 1024, hw.stream_bytes / (t1 - t0) / 1e6,
				hw.stream_bytes / (t2 - t1) / 1e6, 1e9 * hw.clock_cost);
			free(ram);
		}
	}
	hw.tex_dirty = 1;

	hw.width = width;
	hw.height = height;
	hw.fbw = (width + 63) / 64;
	/*
	 * 16-bit frame and depth buffers above 400x300: at 640x480, 32-bit
	 * buffers take 3.6 MiB of the 4 MiB and leave nothing for textures.
	 * PS2GL_FB16=0 or 1 decides instead.
	 */
	hw.fb16 = width * height > 400 * 300;
	env = getenv("PS2GL_FB16");
	if (env)
		hw.fb16 = atoi(env) == 1;
	hw.zscale = hw.fb16 ? 65536.0f : 16777216.0f;
	/* A page is 64x32 pixels with 32 bits, 64x64 with 16 bits */
	frame_pages = hw.fbw * ((height + (hw.fb16 ? 63 : 31)) / (hw.fb16 ? 64 : 32));
	hw.frame_page[0] = 0;
	hw.frame_page[1] = frame_pages;
	hw.zbuf_page = 2 * frame_pages;
	for (i = 0; i < 3 * frame_pages; i++)
		hw.page_owner[i] = PAGE_FRAME;
	hw.draw = 0;
	hw.vsync = 1;
	env = getenv("PS2GL_VSYNC");
	if (env && atoi(env) == 0)
		hw.vsync = 0;
	hw.tex16 = 1;
	env = getenv("PS2GL_TEX32");
	if (env && atoi(env) == 1)
		hw.tex16 = 0;
	env = getenv("PS2GL_VERBOSE");
	hw.verbose = env && atoi(env) >= 1;
	hw.report_print = env && atoi(env) >= 2;
	env = getenv("PS2GL_HUD");
	hw.hud = env && atoi(env) >= 1;
	hw.report = hw.report_print || hw.hud;
	if (hw.hud) {
		hud_font_make();
		memset(hud.line, 0, sizeof(hud.line));
		memset(hud.frame_ms, 0, sizeof(hud.frame_ms));
		hud.frame_at = 0;
		hud.last_swap = 0.0;
	}
	env = getenv("PS2GL_UPLOAD_ROWS");
	hw.upload_rows = env ? (size_t)atoi(env) : 0;
	env = getenv("PS2GL_NOSEND");
	hw.nosend = env && atoi(env) == 1;
	env = getenv("PS2GL_CHECK");
	hw.check_header = env && atoi(env) == 1;
	hw.r_time = now_seconds();
	hw.have_display1 = get_reg(PS2GS_REG_DISPLAY1, &hw.display1) == 0;
	if (!hw.have_display1 && !hw.fake)
		fprintf(stderr, "ps2gl: cannot read DISPLAY1 from the kernel: %s. "
			"The screen stays on the console buffer.\n", strerror(errno));

	hw.tty = open("/dev/tty1", O_RDWR);
	if (hw.tty >= 0 && ioctl(hw.tty, KDSETMODE, KD_GRAPHICS) == 0)
		hw.graphics_mode = 1;
	if (keyboard)
		open_keyboards();

	hw.ready = 1;
	dump_open();
	state_defaults();
	memset(textures, 0, sizeof(textures));
	memset(lum_textures, 0, sizeof(lum_textures));
	init_stream();

	/* Both frame buffers start black. */
	gl.color_mask = 1;
	clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	stream_send(1);
	show_frame(0);
	hw.draw = 1;
	clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	stream_send(1);

	return 0;
}

static void context_close(void)
{
	int i;

	if (!hw.ready)
		return;
	hw.ready = 0;

	stream_send(1);
	dump_close();
	if (hw.fake)
		fprintf(stderr, "ps2gl: %lu sends, %lu words\n", hw.fake_sends,
			hw.fake_words);
	else
		ioctl(hw.fd, PS2GS_IOC_WAIT, PS2GS_WAIT_GIF | PS2GS_WAIT_VU1);
	if (getenv("PS2GL_VERBOSE")) {
		size_t bytes = 0;
		int n8 = 0, n16 = 0, n32 = 0, lums = 0;

		for (i = 1; i <= MAX_TEXTURES; i++) {
			const struct texture *t = &textures[i];

			if (!t->copy)
				continue;
			bytes += t->copy_bytes + (t->clut ? 1024 : 0);
			n8 += t->format == PSMT8;
			n16 += t->format == PSMCT16;
			n32 += t->format == PSMCT32;
			if (lum_textures[i].copy) {
				bytes += lum_textures[i].copy_bytes + 1024;
				lums++;
			}
		}
		fprintf(stderr, "ps2gl: textures in RAM: %zu KiB, %d 8-bit, %d 16-bit, %d 32-bit, %d brightness copies\n",
			bytes / 1024, n8, n16, n32, lums);
	}

	for (i = 1; i <= MAX_TEXTURES; i++) {
		free(textures[i].copy);
		free(lum_textures[i].copy);
	}
	memset(textures, 0, sizeof(textures));
	memset(lum_textures, 0, sizeof(lum_textures));

	hud.font.resident = 0;
	close_keyboards();
	/* The kernel restores the display registers at close. */
	munmap(hw.stream, hw.stream_bytes);
	if (hw.fd >= 0)
		close(hw.fd);
	hw.fd = -1;

	/* The kernel draws the text again when the mode goes to text. */
	if (hw.graphics_mode && hw.tty >= 0)
		ioctl(hw.tty, KDSETMODE, KD_TEXT);
	if (hw.tty >= 0)
		close(hw.tty);
	hw.tty = -1;
}

void ps2glSwapBuffers(void)
{
	state_serial++;
	double t0 = 0.0;

	if (!hw.ready)
		return;
	if (gl.in_begin)
		glEnd();
	if (hw.report)
		t0 = now_seconds();
	if (hw.hud)
		hud_draw();

	stream_send(1);
	if (hw.vsync && !hw.fake)
		ioctl(hw.fd, PS2GS_IOC_VSYNC);
	show_frame(hw.draw);
	hw.draw ^= 1;
	hw.r_frames++;
	if (dump_prologue) {
		dump_prologue = 0;
		init_stream();
	}
	if (hw.report) {
		const double now = now_seconds();

		hw.r_in_swap += now - t0;
		if (now - hw.r_time >= 1.0) {
			const double fps = hw.r_frames / (now - hw.r_time);
			const double frame_ms = 1000.0 * (now - hw.r_time) / hw.r_frames;
			const double in_gl = 1000.0 * hw.r_in_gl / hw.r_frames;
			const double in_swap = 1000.0 * hw.r_in_swap / hw.r_frames;
			const double in_send = 1000.0 * hw.r_in_send / hw.r_frames;

			if (hw.report_print)
				fprintf(stderr, "ps2gl: %.1f frames/s, per frame %.1f sends, %.0f KiB, "
					"%.1f batches, %.1f uploads of %.0f KiB, %.1f evictions, "
					"%.0f ms: %.0f in gl, %.0f in swap, %.0f in the program, "
					"%.0f of them waits for the GS\n",
					fps,
					(double)hw.r_sends / hw.r_frames,
					(double)hw.r_words * 4 / 1024 / hw.r_frames,
					(double)hw.r_batches / hw.r_frames,
					(double)hw.r_uploads / hw.r_frames,
					(double)hw.r_upload_words * 4 / 1024 / hw.r_frames,
					(double)hw.r_evictions / hw.r_frames,
					frame_ms, in_gl, in_swap,
					frame_ms - in_gl - in_swap, in_send);
			if (hw.hud)
				hud_update(fps, frame_ms, in_gl, in_swap, in_send, hw.r_frames);
			hw.r_time = now;
			hw.r_frames = hw.r_sends = hw.r_words = hw.r_batches = 0;
			hw.r_uploads = hw.r_upload_words = hw.r_evictions = 0;
			hw.r_corners = 0;
			hw.r_in_gl = hw.r_in_swap = hw.r_in_send = 0.0;
		}
	}
}

/* The entry points for a window system, for example the SDL2 driver. */

int ps2glCreateContext(int width, int height)
{
	return context_open(width, height, 0);
}

void ps2glDestroyContext(void)
{
	context_close();
}

/* The entry points for a program with no window system. */

static void on_signal(int sig)
{
	ps2glExit();
	signal(sig, SIG_DFL);
	raise(sig);
}

int ps2glInit(void)
{
	static const int signals[] = {
		SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGSEGV, SIGBUS, SIGILL,
		SIGFPE, SIGABRT
	};
	const char *env = getenv("PS2GL_SIZE");
	int width = 320, height = 240;
	unsigned i;

	if (env && sscanf(env, "%dx%d", &width, &height) != 2) {
		width = 320;
		height = 240;
	}
	if (context_open(width, height, 1) < 0)
		return -1;
	for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
		signal(signals[i], on_signal);
	atexit(ps2glExit);
	return 0;
}

void ps2glExit(void)
{
	context_close();
}
