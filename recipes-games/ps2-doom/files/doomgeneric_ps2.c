// SPDX-License-Identifier: GPL-2.0
/*
 * doomgeneric platform code for the PlayStation 2 under Linux.
 *
 * doomgeneric is the Doom port by ozkl (GPL-2.0,
 * https://github.com/ozkl/doomgeneric). This file is its platform layer
 * for the console, under the same license.
 *
 * Display: the game draws a 320x200 picture in RAM. This code sends the
 * picture to the Graphics Synthesizer (GS) as a texture. The GS then draws
 * one textured rectangle that fills the screen, so the GS does the scaling.
 *
 * Input: USB keyboards, through /dev/input/event*.
 *
 * The code maps the hardware through /dev/mem. It puts the console in
 * graphics mode, so the kernel does not draw text during the game.
 *
 * All data for the GS goes through the data memory of vector unit 1 (VU1):
 * the CPU writes a packet there with 32-bit writes, and a 3-instruction VU1
 * program sends it to the GS. A direct write to the GS needs 128-bit
 * registers, and the kernel does not keep those when an interrupt occurs.
 * That method gave random white pixels.
 *
 * Environment variables:
 *   PS2DOOM_FILTER=0   sharp pixels, in place of smooth scaling
 *   PS2DOOM_FRAMES=n   stop after n frames and print the frame rate
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kd.h>

#include "doomgeneric.h"
#include "doomkeys.h"

/* Hardware addresses */
#define GIF_CTRL	0x10003000
#define GIF_STAT	0x10003020
#define VIF1_STAT	0x10003c00
#define VIF1_FBRST	0x10003c10
#define VIF1_FIFO	0x10005000
#define VU1_MICRO	0x11008000
#define VU1_DATA	0x1100c000
#define GS_CSR		0x12001000

/* GS register addresses for the address + data format */
#define GS_PRIM		0x00
#define GS_RGBAQ	0x01
#define GS_UV		0x03
#define GS_XYZ2		0x05
#define GS_TEX0_2	0x07
#define GS_CLAMP_2	0x09
#define GS_TEX1_2	0x15
#define GS_XYOFFSET_2	0x19
#define GS_TEXFLUSH	0x3f
#define GS_SCISSOR_2	0x41
#define GS_TEST_2	0x48
#define GS_FRAME_2	0x4d
#define GS_ZBUF_2	0x4f
#define GS_BITBLTBUF	0x50
#define GS_TRXPOS	0x51
#define GS_TRXREG	0x52
#define GS_TRXDIR	0x53

/* The texture is in GS memory page 300, after the console and its font. */
#define TEXTURE_PAGE	300
#define TEXTURE_BLOCK	(TEXTURE_PAGE * 32)
#define TEXTURE_BW	((DOOMGENERIC_RESX + 63) / 64)

#define IMAGE_QWORDS	(DOOMGENERIC_RESX * DOOMGENERIC_RESY / 4)

/* VU1 data memory holds 1024 quadwords: one tag and the data of a packet. */
#define CHUNK_QWORDS	1000

/*
 * VU1 program: send the packet at the start of the data memory to the GS.
 * Each VU instruction is a pair: lower word first, upper word second.
 */
static const uint32_t vu_program[] = {
	0x10040000, 0x000002ff,	/* vi4 = 0: start of the packet */
	0x800026fc, 0x000002ff,	/* xgkick vi4 */
	0x8000033c, 0x400002ff,	/* end of program */
	0x8000033c, 0x000002ff,
};

/* Screen coordinates get this bias, and XYOFFSET takes it away again. */
#define BIAS 1024

/* The kernel input event for a 32-bit system */
struct key_event {
	uint32_t sec;
	uint32_t usec;
	uint16_t type;
	uint16_t code;
	int32_t value;
};

#define EVENT_KEY	1
#define GRAB_EVENTS	_IOW('E', 0x90, int)
#define MAX_KEYBOARDS	8

static struct {
	int memfd;
	volatile uint32_t *vu;		/* VU1 data memory, 16 KiB */
	volatile uint32_t *gif_ctrl;
	volatile uint32_t *gif_stat;
	volatile uint32_t *vif1_stat;
	volatile uint32_t *vif1_fbrst;
	volatile uint32_t *csr;
	volatile void *vif1_fifo;
	int xres, yres, fbw, psm, yoffset;
	int hardware;		/* The GS and VU1 are mapped */
	int filter;

	int tty;
	int graphics_mode;
	int keyboard[MAX_KEYBOARDS];
	int keyboards;
	int ctrl;

	long frames, max_frames;
	long long start_us;
} ps2;

/* --- hardware access ---------------------------------------------------- */

#ifdef __mips__
static void write128(volatile void *dst, const uint64_t *src)
{
	__asm__ volatile(
		".set push\n"
		".set arch=r5900\n"
		"lq $8, 0(%[src])\n"
		"sq $8, 0(%[dst])\n"
		".set pop\n"
		:
		: [src] "r" (src), [dst] "r" (dst)
		: "$8", "memory");
}
#else	/* For a test on a workstation only */
static void write128(volatile void *dst, const uint64_t *src)
{
	(void)dst;
	(void)src;
}
#endif

static void *map_phys(uint32_t addr, size_t len)
{
	void *m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED,
		ps2.memfd, (off_t)(addr & ~0xfffu));

	return m == MAP_FAILED ? NULL : (char *)m + (addr & 0xfff);
}

static int read_ints(const char *path, const char *format, int *a, int *b)
{
	FILE *f = fopen(path, "r");
	int n;

	if (!f)
		return -1;
	n = fscanf(f, format, a, b);
	fclose(f);
	return n;
}

static long long now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* VU1 runs, the VIF1 has work, or a transfer to the GS is in progress. */
static int vu_busy(void)
{
	return (*ps2.vif1_stat & 0x0f00000f) != 0 ||
	       ((*ps2.gif_stat >> 10) & 3) != 0 ||
	       ((*ps2.gif_stat >> 24) & 0x1f) != 0;
}

/*
 * Start the VU1 program and wait for the end of the transfer to the GS.
 *
 * The start code goes to the VIF1 with one 128-bit write. If an interrupt
 * damages that write, only the low 32 bits arrive. Those bits hold the start
 * code, and the wait code in the next 32 bits is lost. So do not rely on the
 * wait code: look for the start of the work, then for its end.
 */
static void vu_kick(void)
{
	/* VIF1 codes: MSCAL at address 0, then FLUSH */
	static const uint64_t code[2] __attribute__((aligned(16))) = {
		0x1100000014000000ull, 0
	};
	int tries;

	write128(ps2.vif1_fifo, code);
	for (tries = 0; tries < 64 && !vu_busy(); tries++)
		;
	for (tries = 0; tries < 2000000 && vu_busy(); tries++)
		;
}

/* Write one address + data quadword to VU1 data memory. */
static void vu_ad(unsigned q, unsigned addr, uint64_t data)
{
	volatile uint32_t *p = &ps2.vu[4 * q];

	p[0] = (uint32_t)data;
	p[1] = (uint32_t)(data >> 32);
	p[2] = addr;
	p[3] = 0;
}

static void vu_tag(unsigned q, uint64_t lo, uint32_t regs)
{
	volatile uint32_t *p = &ps2.vu[4 * q];

	p[0] = (uint32_t)lo;
	p[1] = (uint32_t)(lo >> 32);
	p[2] = regs;
	p[3] = 0;
}

static void gs_wait_field(void)
{
	const uint32_t field = *ps2.csr & (1u << 13);
	int tries;

	for (tries = 0; tries < 4000000; tries++)
		if ((*ps2.csr & (1u << 13)) != field)
			return;
}

/* Copy the picture from RAM to the texture area of the GS. */
static void upload_picture(void)
{
	const uint32_t *src = (const uint32_t *)DG_ScreenBuffer;
	unsigned done;

	/* Destination area of the transfer */
	vu_tag(0, 4ull | (1ull << 15) | (1ull << 60), 0xe);
	vu_ad(1, GS_BITBLTBUF, ((uint64_t)TEXTURE_BLOCK << 32) |
		((uint64_t)TEXTURE_BW << 48));
	vu_ad(2, GS_TRXPOS, 0);
	vu_ad(3, GS_TRXREG, (uint64_t)DOOMGENERIC_RESX |
		((uint64_t)DOOMGENERIC_RESY << 32));
	vu_ad(4, GS_TRXDIR, 0);
	vu_kick();

	/* The picture data, in packets that fit in the VU1 data memory */
	for (done = 0; done < IMAGE_QWORDS; done += CHUNK_QWORDS) {
		const unsigned n = IMAGE_QWORDS - done < CHUNK_QWORDS ?
			IMAGE_QWORDS - done : CHUNK_QWORDS;
		volatile uint32_t *dst = &ps2.vu[4];
		unsigned i;

		/* Image data tag: n quadwords, end of packet */
		vu_tag(0, (uint64_t)n | (1ull << 15) | (2ull << 58), 0);

		/* The game has blue in the low byte. The GS has red there. */
		for (i = 0; i < 4 * n; i++) {
			const uint32_t p = *src++;

			dst[i] = 0x80000000u | (p & 0x0000ff00) |
				((p >> 16) & 0xff) | ((p & 0xff) << 16);
		}
		vu_kick();
	}
}

/*
 * Draw the texture on the full screen.
 *
 * The console scrolls with a display offset: screen line s shows buffer line
 * (s + yoffset) % yres. A picture that crosses the end of the buffer needs
 * two passes with different offsets.
 */
static void draw_picture(void)
{
	const int yo = ((ps2.yoffset % ps2.yres) + ps2.yres) % ps2.yres;
	const int shift[2] = { yo, yo - ps2.yres };
	const int passes = yo ? 2 : 1;
	int p;

	for (p = 0; p < passes; p++) {
		int y0 = shift[p], y1 = ps2.yres - 1 + shift[p];
		unsigned q = 1;

		if (y0 < 0)
			y0 = 0;
		if (y1 > ps2.yres - 1)
			y1 = ps2.yres - 1;

		vu_ad(q++, GS_FRAME_2, ((uint64_t)ps2.fbw << 16) |
			((uint64_t)ps2.psm << 24));
		vu_ad(q++, GS_ZBUF_2, 1ull << 32);		/* No depth writes */
		vu_ad(q++, GS_XYOFFSET_2, (uint64_t)(BIAS << 4) |
			((uint64_t)((BIAS - shift[p]) << 4) << 32));
		vu_ad(q++, GS_SCISSOR_2, ((uint64_t)(ps2.xres - 1) << 16) |
			((uint64_t)y0 << 32) | ((uint64_t)y1 << 48));
		vu_ad(q++, GS_TEST_2, (1ull << 16) | (1ull << 17));

		/* Texture: 32-bit color, 512x256 texels, color replaces the pixel */
		vu_ad(q++, GS_TEX0_2, (uint64_t)TEXTURE_BLOCK |
			((uint64_t)TEXTURE_BW << 14) | (9ull << 26) | (8ull << 30) |
			(1ull << 35));
		vu_ad(q++, GS_TEX1_2, 1 | (ps2.filter ? (1 << 5) | (1 << 6) : 0));
		/* Do not read texels outside the picture. */
		vu_ad(q++, GS_CLAMP_2, 2 | (2 << 2) |
			((uint64_t)(DOOMGENERIC_RESX - 1) << 14) |
			((uint64_t)(DOOMGENERIC_RESY - 1) << 34));
		vu_ad(q++, GS_TEXFLUSH, 0);

		/* Sprite with texture, texel coordinates, context 2 */
		vu_ad(q++, GS_PRIM, 6 | (1 << 4) | (1 << 8) | (1 << 9));
		vu_ad(q++, GS_RGBAQ, 0x80808080ull | (0x3f800000ull << 32));
		vu_ad(q++, GS_UV, 8 | (8 << 16));
		vu_ad(q++, GS_XYZ2, (uint64_t)(BIAS << 4) | ((uint64_t)(BIAS << 4) << 16));
		vu_ad(q++, GS_UV, (uint64_t)((DOOMGENERIC_RESX << 4) - 8) |
			((uint64_t)((DOOMGENERIC_RESY << 4) - 8) << 16));
		vu_ad(q++, GS_XYZ2, (uint64_t)((BIAS + ps2.xres) << 4) |
			((uint64_t)((BIAS + ps2.yres) << 4) << 16));

		vu_tag(0, (uint64_t)(q - 1) | (1ull << 15) | (1ull << 60), 0xe);
		vu_kick();
	}
}

/* --- console and keyboard ----------------------------------------------- */

static void restore_console(void)
{
	int i;

	/*
	 * If the game stops in the middle of a transfer, the GS waits for the
	 * remaining data and reads the next console text as picture data. A
	 * reset of the VIF1 and the GIF puts both back in the idle state.
	 */
	if (ps2.hardware) {
		for (i = 0; i < 100000 && vu_busy(); i++)
			;
		*ps2.vif1_fbrst = 1;
		*ps2.gif_ctrl = 1;
	}

	for (i = 0; i < ps2.keyboards; i++)
		ioctl(ps2.keyboard[i], GRAB_EVENTS, 0);
	if (ps2.graphics_mode) {
		/* The kernel draws the text again when the mode goes to text. */
		ioctl(ps2.tty, KDSETMODE, KD_TEXT);
		ps2.graphics_mode = 0;
	}
}

static void at_exit(void)
{
	restore_console();

	if (ps2.max_frames && ps2.frames) {
		const long long us = now_us() - ps2.start_us;

		printf("%ld frames in %lld ms: %lld frames each second\n",
			ps2.frames, us / 1000,
			us > 0 ? ps2.frames * 1000000ll / us : 0);
	}
}

static void on_signal(int sig)
{
	restore_console();
	signal(sig, SIG_DFL);
	raise(sig);
}

static void open_keyboards(void)
{
	char path[32];
	int i;

	for (i = 0; i < 16 && ps2.keyboards < MAX_KEYBOARDS; i++) {
		int fd;

		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		/* Keep the keys away from the text console. */
		ioctl(fd, GRAB_EVENTS, 1);
		ps2.keyboard[ps2.keyboards++] = fd;
	}

	if (!ps2.keyboards)
		fprintf(stderr, "ps2-doom: no keyboard in /dev/input\n");
}

/* Linux key code to Doom key. Return 0 for a key with no function. */
static unsigned char doom_key(unsigned code)
{
	static const char row1[] = "1234567890";	/* Codes 2 to 11 */
	static const char row2[] = "qwertyuiop";	/* Codes 16 to 25 */
	static const char row3[] = "asdfghjkl";		/* Codes 30 to 38 */
	static const char row4[] = "zxcvbnm";		/* Codes 44 to 50 */

	if (code >= 2 && code <= 11)
		return row1[code - 2];
	if (code >= 16 && code <= 25)
		return row2[code - 16];
	if (code >= 30 && code <= 38)
		return row3[code - 30];
	if (code >= 44 && code <= 50)
		return row4[code - 44];
	if (code >= 59 && code <= 68)
		return KEY_F1 + (code - 59);

	switch (code) {
	case 1:		return KEY_ESCAPE;
	case 12:	return KEY_MINUS;
	case 13:	return KEY_EQUALS;
	case 14:	return KEY_BACKSPACE;
	case 15:	return KEY_TAB;
	case 28:	return KEY_ENTER;
	case 96:	return KEY_ENTER;	/* Keypad enter */
	case 29:	return KEY_FIRE;	/* Left control */
	case 97:	return KEY_FIRE;	/* Right control */
	case 42:	return KEY_RSHIFT;	/* Left shift: run */
	case 54:	return KEY_RSHIFT;
	case 56:	return KEY_RALT;	/* Left alt: strafe */
	case 100:	return KEY_RALT;
	case 57:	return KEY_USE;		/* Space */
	case 51:	return KEY_STRAFE_L;	/* Comma */
	case 52:	return KEY_STRAFE_R;	/* Period */
	case 87:	return KEY_F11;
	case 88:	return KEY_F12;
	case 103:	return KEY_UPARROW;
	case 105:	return KEY_LEFTARROW;
	case 106:	return KEY_RIGHTARROW;
	case 108:	return KEY_DOWNARROW;
	case 102:	return KEY_HOME;
	case 104:	return KEY_PGUP;
	case 107:	return KEY_END;
	case 109:	return KEY_PGDN;
	case 110:	return KEY_INS;
	case 111:	return KEY_DEL;
	case 119:	return KEY_PAUSE;
	default:	return 0;
	}
}

/* --- doomgeneric interface ---------------------------------------------- */

void DG_Init(void)
{
	static const int signals[] = {
		SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGSEGV, SIGBUS, SIGILL,
		SIGFPE, SIGABRT
	};
	const char *s;
	int bpp = 32, unused = 0, xo = 0;
	unsigned i;

	s = getenv("PS2DOOM_FILTER");
	ps2.filter = !s || atoi(s) != 0;
	s = getenv("PS2DOOM_FRAMES");
	ps2.max_frames = s ? atol(s) : 0;

	ps2.xres = 576;
	ps2.yres = 460;
	read_ints("/sys/class/graphics/fb0/virtual_size", "%d,%d",
		&ps2.xres, &ps2.yres);
	read_ints("/sys/class/graphics/fb0/bits_per_pixel", "%d", &bpp, &unused);
	if (read_ints("/sys/class/graphics/fb0/pan", "%d,%d", &xo, &ps2.yoffset) != 2)
		ps2.yoffset = 0;
	ps2.fbw = (ps2.xres + 63) / 64;
	ps2.psm = bpp == 32 ? 0x00 : 0x02;

#ifdef __mips__
	ps2.memfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (ps2.memfd < 0) {
		perror("ps2-doom: /dev/mem");
		exit(1);
	}
	{
		volatile uint32_t *micro = map_phys(VU1_MICRO, 0x1000);
		unsigned k;

		ps2.vu = map_phys(VU1_DATA, 0x4000);
		ps2.gif_ctrl = map_phys(GIF_CTRL, 0x1000);
		ps2.vif1_stat = map_phys(VIF1_STAT, 0x1000);
		ps2.vif1_fifo = map_phys(VIF1_FIFO, 0x1000);
		ps2.csr = map_phys(GS_CSR, 0x1000);
		if (!micro || !ps2.vu || !ps2.gif_ctrl || !ps2.vif1_stat ||
		    !ps2.vif1_fifo || !ps2.csr) {
			fprintf(stderr, "ps2-doom: cannot map the hardware\n");
			exit(1);
		}
		/* These registers are in the same pages. */
		ps2.gif_stat = (volatile uint32_t *)((volatile char *)ps2.gif_ctrl +
			(GIF_STAT - GIF_CTRL));
		ps2.vif1_fbrst = (volatile uint32_t *)((volatile char *)ps2.vif1_stat +
			(VIF1_FBRST - VIF1_STAT));

		*ps2.vif1_fbrst = 1;
		usleep(1000);
		for (k = 0; k < sizeof(vu_program) / sizeof(vu_program[0]); k++)
			micro[k] = vu_program[k];
	}
	ps2.hardware = 1;
#else
	(void)map_phys;
	(void)vu_program;
#endif

	atexit(at_exit);
	for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
		signal(signals[i], on_signal);

	open_keyboards();

	/* Graphics mode stops the text and the cursor of the console. */
	ps2.tty = open("/dev/tty0", O_RDWR);
	if (ps2.tty >= 0 && ps2.hardware &&
	    ioctl(ps2.tty, KDSETMODE, KD_GRAPHICS) == 0)
		ps2.graphics_mode = 1;

	ps2.start_us = now_us();
}

void DG_DrawFrame(void)
{
	if (ps2.hardware) {
		upload_picture();
		gs_wait_field();
		draw_picture();
	}

	ps2.frames++;
	if (ps2.max_frames && ps2.frames >= ps2.max_frames)
		exit(0);
}

void DG_SleepMs(uint32_t ms)
{
	usleep(ms * 1000);
}

uint32_t DG_GetTicksMs(void)
{
	return (uint32_t)(now_us() / 1000);
}

int DG_GetKey(int *pressed, unsigned char *key)
{
	struct key_event ev;
	int i;

	for (i = 0; i < ps2.keyboards; i++) {
		while (read(ps2.keyboard[i], &ev, sizeof(ev)) == sizeof(ev)) {
			unsigned char k;

			/* Value 0 = release, 1 = press, 2 = repeat */
			if (ev.type != EVENT_KEY || ev.value > 1)
				continue;

			if (ev.code == 29 || ev.code == 97)
				ps2.ctrl = ev.value;
			/* Control + C stops the game at once. */
			if (ps2.ctrl && ev.code == 46 && ev.value == 1)
				exit(0);

			k = doom_key(ev.code);
			if (!k)
				continue;
			*pressed = ev.value;
			*key = k;
			return 1;
		}
	}

	return 0;
}

void DG_SetWindowTitle(const char *title)
{
	(void)title;
}

int main(int argc, char **argv)
{
	doomgeneric_Create(argc, argv);

	for (;;)
		doomgeneric_Tick();

	return 0;
}
