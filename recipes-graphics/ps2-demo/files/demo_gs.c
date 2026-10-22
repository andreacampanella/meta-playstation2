// SPDX-License-Identifier: MIT
/*
 * The direct back end of the demo: the GS through /dev/ps2gs, with no
 * libGL and no VU1. The CPU transforms the vertices, clips the primitives
 * against the six planes, divides by w, calculates the fog factor and
 * writes GIF packets (address + data for the registers, PACKED for the
 * vertices, IMAGE for the textures) to the GIF channel.
 *
 * The GS settings follow the same rules as libGL.so.1: context 2, two
 * frame buffers of PSMCT32, a PSMZ24 depth buffer, 16-bit textures with
 * TEXA for the alpha bit, XYZF2 vertices with a 24-bit depth, the same
 * blend and test registers. The picture must match the OpenGL back end.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kd.h>
#include <linux/ps2gs.h>

#include "demo.h"
#include "demo_main.h"

/* --- GS registers ----------------------------------------------------------- */

#define GS_PRIM		0x00
#define GS_RGBAQ	0x01
#define GS_XYZ2		0x05
#define GS_TEX0_2	0x07
#define GS_CLAMP_2	0x09
#define GS_TEX1_2	0x15
#define GS_XYOFFSET_2	0x19
#define GS_PRMODECONT	0x1a
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

#define PSMCT32		0x00
#define PSMCT16		0x02
#define PSMZ24		0x01	/* ZBUF has 4 bits for PSM: PSMZ24 0x31 is 0x1 there */

#define BIAS		1024	/* Pixels: the screen starts here in GS coordinates */
#define GUARD		4.0f	/* The guard band: GUARD times the half screen */
#define ZSCALE		16777216.0f

#define STREAM_BYTES	(256 * 1024)
#define MAX_VERTICES	4096

/* --- the device and the packet buffer ------------------------------------------ */

/*
 * PC test: with DEMO_FAKE the packets go to the GS model of the libGL test
 * bench (tests/gsmodel.c) instead of the device, wrapped as a DIRECT code.
 */
#ifdef DEMO_FAKE
void ps2gl_model_init(int width, int height);
void ps2gl_model_stream(const uint32_t *words, size_t count);
void ps2gl_model_reg(uint32_t reg, uint64_t value);
#endif

static struct {
	int fd, tty, graphics_mode;
	uint32_t *stream;
	size_t stream_bytes, capacity, used;
	int width, height, fbw;
	unsigned frame_page[2], zbuf_page, pool_page, next_page;
	int draw;
	uint64_t display1;
	int have_display1, verbose;
	int keyboard[8], keyboards;
} hw;

static void send(int wait)
{
	struct ps2gs_send s;

	if (!hw.used)
		return;
	while (hw.used & 3)
		hw.stream[hw.used++] = 0;
#ifdef DEMO_FAKE
	{
		uint32_t *w = malloc(4 * (hw.used + 4));

		w[0] = w[1] = w[2] = 0;
		w[3] = 0x50000000u | ((hw.used / 4) & 0xffff);
		memcpy(&w[4], hw.stream, 4 * hw.used);
		ps2gl_model_stream(w, hw.used + 4);
		free(w);
		hw.used = 0;
		(void)s;
		(void)wait;
		return;
	}
#endif
	s.offset = 0;
	s.qwc = hw.used / 4;
	s.channel = PS2GS_CHANNEL_GIF;
	s.flags = wait ? PS2GS_SEND_WAIT : 0;
	if (ioctl(hw.fd, PS2GS_IOC_SEND, &s) < 0)
		fprintf(stderr, "ps2demo-gs: send: %s\n", strerror(errno));
	hw.used = 0;
}

static uint32_t *reserve(size_t words)
{
	if (hw.used + words > hw.capacity)
		send(1);
	if (hw.used + words > hw.capacity)
		return NULL;
	hw.used += words;
	return &hw.stream[hw.used - words];
}

/* An address + data packet with n registers. Returns the first data qword. */
static uint64_t *packet_ad(unsigned n)
{
	uint64_t *q = (uint64_t *)reserve(4 * (n + 1));

	if (!q)
		return NULL;
	q[0] = (uint64_t)n | (1ull << 15) | (1ull << 60);	/* NLOOP, EOP, NREG 1 */
	q[1] = 0xe;						/* A + D */
	return q + 2;
}

static void ad(uint64_t *q, unsigned reg, uint64_t value)
{
	q[0] = value;
	q[1] = reg;
}

static int set_reg(unsigned reg, uint64_t value)
{
	struct ps2gs_reg r = { reg, 0, value };

#ifdef DEMO_FAKE
	ps2gl_model_reg(reg, value);
	return 0;
#endif
	return ioctl(hw.fd, PS2GS_IOC_SET_REG, &r);
}

static int get_reg(unsigned reg, uint64_t *value)
{
	struct ps2gs_reg r = { reg, 0, 0 };

	if (ioctl(hw.fd, PS2GS_IOC_GET_REG, &r) < 0)
		return -1;
	*value = r.value;
	return 0;
}

/* --- textures ------------------------------------------------------------------ */

struct texture {
	int used, width, height, tw, th, tbw;
	int bits32;			/* Alpha between the ends: 32-bit pixels, as the library does */
	unsigned page, lum_page;	/* The pixels, and the multiply copy (32-bit) */
	int resident, lum_resident;	/* The copy is in GS memory now */
	uint16_t *pixels;
	uint32_t *pixels32, *lum;
};

static struct texture textures[DEMO_TEX_COUNT + 1];

static int log2_of(int n)
{
	int l = 0;

	while ((1 << l) < n)
		l++;
	return l;
}

static void upload(unsigned page, int tbw, int format, int width, int height,
	const void *data, size_t bytes)
{
	const size_t qwords = (bytes + 15) / 16;
	const uint32_t *src = data;
	size_t done = 0;
	uint64_t *q;

	q = packet_ad(4);
	if (!q)
		return;
	ad(q, GS_BITBLTBUF, ((uint64_t)(page * 32) << 32) |
		((uint64_t)tbw << 48) | ((uint64_t)format << 56));
	ad(q + 2, GS_TRXPOS, 0);
	ad(q + 4, GS_TRXREG, (uint64_t)width | ((uint64_t)height << 32));
	ad(q + 6, GS_TRXDIR, 0);

	while (done < qwords) {
		size_t n = qwords - done;
		uint64_t *tag;
		uint32_t *d;

		if (n > 16384)
			n = 16384;
		if (n > hw.capacity / 4 - 8)
			n = hw.capacity / 4 - 8;
		if (hw.used + 4 * (n + 1) > hw.capacity)
			send(1);
		tag = (uint64_t *)reserve(4);
		d = reserve(4 * n);
		if (!tag || !d)
			return;
		tag[0] = (uint64_t)n | (1ull << 15) | (2ull << 58);	/* IMAGE */
		tag[1] = 0;
		memcpy(d, src + 4 * done, 16 * n);
		done += n;
	}
	/* The texture cache may hold old pixels */
	q = packet_ad(1);
	if (q)
		ad(q, GS_TEXFLUSH, 0);
}

void d_texture_create(int id, int width, int height, const uint8_t *rgba)
{
	struct texture *t = &textures[id];
	int i;

	t->used = 1;
	t->width = width;
	t->height = height;
	t->tw = log2_of(width);
	t->th = log2_of(height);
	t->tbw = (width + 63) / 64;
	free(t->pixels);
	free(t->pixels32);
	free(t->lum);
	t->pixels = malloc((size_t)width * height * 2);
	t->pixels32 = malloc((size_t)width * height * 4);
	t->lum = malloc((size_t)width * height * 4);
	t->bits32 = 0;
	for (i = 0; i < width * height; i++) {
		const uint8_t *c = &rgba[4 * i];
		const unsigned gray = (c[0] * 77 + c[1] * 151 + c[2] * 28) >> 8;

		if (c[3] > 16 && c[3] < 240)
			t->bits32 = 1;
		t->pixels[i] = (c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10) |
			(c[3] >= 128 ? 0x8000 : 0);
		t->pixels32[i] = c[0] | c[1] << 8 | c[2] << 16 | (uint32_t)((c[3] + 1) >> 1) << 24;
		t->lum[i] = c[0] | c[1] << 8 | c[2] << 16 | gray << 24;
	}
	t->resident = 0;
	t->lum_resident = 0;
}

/* New pixels in a part of the texture: the whole texture goes up again */
void d_texture_update(int id, int x, int y, int width, int height, const uint8_t *rgba)
{
	struct texture *t = &textures[id];
	int i, j;

	if (!t->used)
		return;
	for (j = 0; j < height; j++)
		for (i = 0; i < width; i++) {
			const uint8_t *c = &rgba[4 * (width * j + i)];
			const unsigned gray = (c[0] * 77 + c[1] * 151 + c[2] * 28) >> 8;
			const int k = t->width * (y + j) + x + i;

			t->pixels[k] = (c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10) |
				(c[3] >= 128 ? 0x8000 : 0);
			t->pixels32[k] = c[0] | c[1] << 8 | c[2] << 16 | (uint32_t)((c[3] + 1) >> 1) << 24;
			t->lum[k] = c[0] | c[1] << 8 | c[2] << 16 | gray << 24;
		}
	t->resident = 0;
	t->lum_resident = 0;
}

/*
 * GS memory for textures: the pages after the frame and depth buffers, up
 * to the end of the 4 MiB. A texture goes in when a draw needs it. When
 * the pool is full, all textures leave it and the pool starts again from
 * the first page. The GS reads the packet in order, so a new upload over a
 * texture that an earlier draw used is safe. The library has a smarter
 * eviction. This one is the simple reference.
 */
#define GS_PAGES	512

static unsigned pool_alloc(unsigned pages)
{
	int i;

	if (hw.next_page + pages > GS_PAGES) {
		for (i = 0; i <= DEMO_TEX_COUNT; i++)
			textures[i].resident = textures[i].lum_resident = 0;
		hw.next_page = hw.pool_page;
		if (hw.verbose)
			fprintf(stderr, "ps2demo-gs: texture pool full, all textures out\n");
	}
	hw.next_page += pages;
	return hw.next_page - pages;
}

/* A page is 8 KiB: 64x64 pixels with 16 bits, 64x32 with 32 bits */
static void texture_resident(struct texture *t, int multiply)
{
	const int w = t->width, h = t->height;

	if (multiply) {
		if (t->lum_resident)
			return;
		t->lum_page = pool_alloc(t->tbw * ((h + 31) / 32));
		upload(t->lum_page, t->tbw, PSMCT32, w, h, t->lum, (size_t)w * h * 4);
		t->lum_resident = 1;
		return;
	}
	if (t->resident)
		return;
	t->page = pool_alloc(t->tbw * ((h + (t->bits32 ? 31 : 63)) / (t->bits32 ? 32 : 64)));
	if (t->bits32)
		upload(t->page, t->tbw, PSMCT32, w, h, t->pixels32, (size_t)w * h * 4);
	else
		upload(t->page, t->tbw, PSMCT16, w, h, t->pixels, (size_t)w * h * 2);
	t->resident = 1;
}

/* --- state --------------------------------------------------------------------- */

static struct {
	float projection[16], modelview[16], clip[16];
	int depth_test, depth_write;
	enum demo_blend blend;
	int alpha_test;
	float alpha_ref;
	enum demo_fog fog;
	float fog_density, fog_start, fog_end, fog_color[3];
	int color_mask;
	int texture, linear, clamp;
	int smooth;
	float color[4], texcoord[2];
	enum demo_prim prim;
	int count;
	float v[MAX_VERTICES][9];	/* x, y, z, s, t, r, g, b, a */
} st;

void d_clear(float r, float g, float b)
{
	const uint32_t rgb = (uint32_t)(r * 255.0f) | (uint32_t)(g * 255.0f) << 8 |
		(uint32_t)(b * 255.0f) << 16;
	const uint32_t z = 16;	/* The far depth: 16 + (ZSCALE - 32) * (1 - 1) */
	uint64_t *q = packet_ad(9);

	if (!q)
		return;
	ad(q + 0, GS_FRAME_2, (uint64_t)hw.frame_page[hw.draw] | ((uint64_t)hw.fbw << 16) |
		((uint64_t)PSMCT32 << 24));
	ad(q + 2, GS_ZBUF_2, (uint64_t)hw.zbuf_page | ((uint64_t)PSMZ24 << 24));
	ad(q + 4, GS_XYOFFSET_2, (uint64_t)(BIAS << 4) | ((uint64_t)(BIAS << 4) << 32));
	ad(q + 6, GS_SCISSOR_2, (uint64_t)(hw.width - 1) << 16 | ((uint64_t)(hw.height - 1) << 48));
	ad(q + 8, GS_TEST_2, (1ull << 16) | (1ull << 17));	/* Depth test: always */
	ad(q + 10, GS_PRIM, 6 | (1 << 9));			/* A sprite, context 2 */
	ad(q + 12, GS_RGBAQ, (uint64_t)rgb | (0x80ull << 24) | (0x3f800000ull << 32));
	ad(q + 14, GS_XYZ2, (uint64_t)(BIAS << 4) | ((uint64_t)(BIAS << 4) << 16) |
		((uint64_t)z << 32));
	ad(q + 16, GS_XYZ2, (uint64_t)((BIAS + hw.width) << 4) |
		((uint64_t)((BIAS + hw.height) << 4) << 16) | ((uint64_t)z << 32));
}

void d_frustum(float l, float r, float b, float t, float n, float f)
{
	m_frustum(st.projection, l, r, b, t, n, f);
	m_multiply(st.clip, st.projection, st.modelview);
}

void d_ortho(float l, float r, float b, float t, float n, float f)
{
	m_ortho(st.projection, l, r, b, t, n, f);
	m_multiply(st.clip, st.projection, st.modelview);
}

void d_modelview(const float m[16])
{
	memcpy(st.modelview, m, sizeof(st.modelview));
	m_multiply(st.clip, st.projection, st.modelview);
}

void d_depth(int test, int write)
{
	st.depth_test = test;
	st.depth_write = write;
}

void d_blend(enum demo_blend mode)
{
	st.blend = mode;
}

void d_alpha_test(int on, float ref)
{
	st.alpha_test = on;
	st.alpha_ref = ref;
}

void d_fog(enum demo_fog mode, float density, float start, float end,
	const float color[3])
{
	st.fog = mode;
	st.fog_density = density;
	st.fog_start = start;
	st.fog_end = end;
	if (color)
		memcpy(st.fog_color, color, sizeof(st.fog_color));
}

void d_color_mask(int on)
{
	st.color_mask = on;
}

void d_texture(int id, int linear, int clamp)
{
	st.texture = id;
	st.linear = linear;
	st.clamp = clamp;
}

void d_smooth(int on)
{
	st.smooth = on;
}

void d_begin(enum demo_prim prim)
{
	st.prim = prim;
	st.count = 0;
}

void d_color(float r, float g, float b, float a)
{
	st.color[0] = r;
	st.color[1] = g;
	st.color[2] = b;
	st.color[3] = a;
}

void d_texcoord(float s, float t)
{
	st.texcoord[0] = s;
	st.texcoord[1] = t;
}

void d_vertex(float x, float y, float z)
{
	float *v;

	if (st.count >= MAX_VERTICES)
		return;
	v = st.v[st.count++];
	v[0] = x;
	v[1] = y;
	v[2] = z;
	v[3] = st.texcoord[0];
	v[4] = st.texcoord[1];
	v[5] = st.color[0];
	v[6] = st.color[1];
	v[7] = st.color[2];
	v[8] = st.color[3];
}

/* --- the pipeline --------------------------------------------------------------- */

/* A vertex in clip space with its attributes */
struct cv {
	float x, y, z, w;	/* x and y divided by GUARD */
	float s, t, d;		/* d: the fog distance, -z in eye space */
	float r, g, b, a;
};

static uint8_t color_byte(float c)
{
	return c <= 0.0f ? 0 : c >= 1.0f ? 255 : (uint8_t)(c * 255.0f);
}

static void transform(const float *in, struct cv *out)
{
	const float *m = st.clip, *mv = st.modelview;
	const float x = in[0], y = in[1], z = in[2];

	out->x = (m[0] * x + m[4] * y + m[8] * z + m[12]) / GUARD;
	out->y = (m[1] * x + m[5] * y + m[9] * z + m[13]) / GUARD;
	out->z = m[2] * x + m[6] * y + m[10] * z + m[14];
	out->w = m[3] * x + m[7] * y + m[11] * z + m[15];
	out->d = -(mv[2] * x + mv[6] * y + mv[10] * z + mv[14]);
	if (out->d < 0.0f)
		out->d = 0.0f;
	out->s = in[3];
	out->t = in[4];
	out->r = in[5];
	out->g = in[6];
	out->b = in[7];
	out->a = in[8];
}

/* The six planes: inside is where plane . (x, y, z, w) >= 0 */
static const float planes[6][4] = {
	{ -1, 0, 0, 1 }, { 1, 0, 0, 1 }, { 0, -1, 0, 1 }, { 0, 1, 0, 1 },
	{ 0, 0, -1, 1 }, { 0, 0, 1, 1 },
};

static float distance(const struct cv *v, const float *p)
{
	return p[0] * v->x + p[1] * v->y + p[2] * v->z + p[3] * v->w;
}

static void lerp(struct cv *out, const struct cv *a, const struct cv *b, float t)
{
	const float *fa = (const float *)a, *fb = (const float *)b;
	float *fo = (float *)out;
	unsigned i;

	for (i = 0; i < sizeof(*a) / sizeof(float); i++)
		fo[i] = fa[i] + (fb[i] - fa[i]) * t;
}

static unsigned clip_flags(const struct cv *v)
{
	const float w = fabsf(v->w);
	unsigned f = 0;

	if (v->x > w) f |= 1;
	if (v->x < -w) f |= 2;
	if (v->y > w) f |= 4;
	if (v->y < -w) f |= 8;
	if (v->z > w) f |= 16;
	if (v->z < -w) f |= 32;
	return f;
}

/* Sutherland-Hodgman: clip the polygon against the planes in mask. */
static int clip_polygon(struct cv *poly, int n, unsigned mask)
{
	struct cv out[16];
	int p;

	if (mask & 0x20)
		mask = 0x3f;
	for (p = 0; p < 6 && n; p++) {
		int i, m = 0;

		if (!(mask & (1 << p)))
			continue;
		for (i = 0; i < n; i++) {
			const struct cv *a = &poly[i], *b = &poly[(i + 1) % n];
			const float da = distance(a, planes[p]), db = distance(b, planes[p]);

			if (da >= 0.0f)
				out[m++] = *a;
			if (da * db < 0.0f)
				lerp(&out[m++], a, b, da / (da - db));
		}
		memcpy(poly, out, m * sizeof(*poly));
		n = m;
	}
	return n;
}

/* The texture coordinate move of the triangle in progress (see emit_triangle) */
static float st_move[2];

/* The output packet: a GIF tag with PACKED ST, RGBAQ, XYZF2 */
static uint32_t *out_tag;
static unsigned out_count;
static unsigned out_prim;

static void out_begin(unsigned prim)
{
	out_tag = reserve(4);
	if (!out_tag)
		return;
	out_prim = prim;
	out_count = 0;
}

static void out_end(void)
{
	if (!out_tag)
		return;
	if (!out_count) {
		hw.used -= 4;		/* Remove the empty tag */
	} else {
		const uint64_t tag = (uint64_t)out_count | (1ull << 15) | (1ull << 46) |
			((uint64_t)out_prim << 47) | (3ull << 60);

		memcpy(out_tag, &tag, 8);
		((uint64_t *)out_tag)[1] = 0x412;
	}
	out_tag = NULL;
}

/* Project a clip space vertex and write it to the packet */
static void out_vertex(const struct cv *v)
{
	const float q = 1.0f / v->w;
	const float sx = BIAS + hw.width * 0.5f + v->x * GUARD * (hw.width * 0.5f) * q;
	const float sy = BIAS + hw.height * 0.5f - v->y * GUARD * (hw.height * 0.5f) * q;
	const float zn = v->z * q;				/* -1 to 1 */
	const float sz = 16.0f + (ZSCALE - 32.0f) * (1.0f - (zn + 1.0f) * 0.5f);
	const int textured = st.texture != 0;
	float f = 1.0f;
	uint32_t *p;
	int32_t ix, iy, iz, fog;

	if (st.fog == DEMO_FOG_LINEAR)
		f = (st.fog_end - v->d) / (st.fog_end - st.fog_start);
	else if (st.fog == DEMO_FOG_EXP)
		f = expf(-st.fog_density * v->d);
	else if (st.fog == DEMO_FOG_EXP2)
		f = expf(-(st.fog_density * v->d) * (st.fog_density * v->d));
	if (f < 0.0f)
		f = 0.0f;
	if (f > 1.0f)
		f = 1.0f;

	/*
	 * The packet is full when it reaches the buffer: close it and open
	 * the next one. 12 words for a vertex.
	 */
	if (hw.used + 12 > hw.capacity) {
		out_end();
		send(1);
		out_begin(out_prim);
	}
	p = reserve(12);
	if (!p)
		return;
	/* ST: s * q, t * q, q */
	{
		const float sq = (v->s - st_move[0]) * q, tq = (v->t - st_move[1]) * q;

		memcpy(&p[0], &sq, 4);
		memcpy(&p[1], &tq, 4);
		memcpy(&p[2], &q, 4);
		p[3] = 0;
	}
	/* RGBAQ: 128 is 1.0 with a texture, 255 without. Alpha: 128 is 1.0. */
	if (textured) {
		p[4] = (color_byte(v->r) + 1) >> 1;
		p[5] = (color_byte(v->g) + 1) >> 1;
		p[6] = (color_byte(v->b) + 1) >> 1;
	} else {
		p[4] = color_byte(v->r);
		p[5] = color_byte(v->g);
		p[6] = color_byte(v->b);
	}
	p[7] = (color_byte(v->a) + 1) >> 1;
	/* XYZF2: 12.4 fixed point x and y, 24-bit z in bits 4-27, fog in bits 4-11 */
	ix = (int32_t)(sx * 16.0f);
	iy = (int32_t)(sy * 16.0f);
	iz = (int32_t)sz;
	fog = (int32_t)(f * 255.0f);
	p[8] = (uint32_t)ix & 0xffff;
	p[9] = (uint32_t)iy & 0xffff;
	p[10] = (uint32_t)iz << 4;
	p[11] = (uint32_t)fog << 4;
	out_count++;
}

/* The register settings for the primitives that follow */
static void apply_state(void)
{
	const int textured = st.texture != 0;
	struct texture *t = &textures[st.texture];
	const int multiply = st.blend == DEMO_BLEND_MULTIPLY && textured;
	uint64_t *q;
	uint64_t alpha, test = 0;

	if (textured)
		texture_resident(t, multiply);
	q = packet_ad(13);
	if (!q)
		return;
	ad(q + 0, GS_FRAME_2, (uint64_t)hw.frame_page[hw.draw] | ((uint64_t)hw.fbw << 16) |
		((uint64_t)PSMCT32 << 24) | (st.color_mask ? 0 : 0xffffffffull << 32));
	ad(q + 2, GS_ZBUF_2, (uint64_t)hw.zbuf_page | ((uint64_t)PSMZ24 << 24) |
		(st.depth_write ? 0 : 1ull << 32));
	ad(q + 4, GS_XYOFFSET_2, (uint64_t)(BIAS << 4) | ((uint64_t)(BIAS << 4) << 32));
	ad(q + 6, GS_SCISSOR_2, (uint64_t)(hw.width - 1) << 16 | ((uint64_t)(hw.height - 1) << 48));

	if (st.alpha_test) {
		unsigned ref = (unsigned)(st.alpha_ref * 128.0f + 0.5f);

		test |= 1 | (6 << 1) | (ref << 4);	/* ATST greater */
	}
	test |= 1ull << 16;
	test |= (uint64_t)(st.depth_test ? 2 : 1) << 17;	/* GEQUAL on the GS = LEQUAL in GL */
	ad(q + 8, GS_TEST_2, test);

	if (textured) {
		const unsigned page = multiply ? t->lum_page : t->page;
		const unsigned format = (multiply || t->bits32) ? PSMCT32 : PSMCT16;

		ad(q + 10, GS_TEX0_2, (uint64_t)(page * 32) | ((uint64_t)t->tbw << 14) |
			((uint64_t)format << 20) | ((uint64_t)t->tw << 26) |
			((uint64_t)t->th << 30) | (1ull << 34));
		ad(q + 12, GS_TEX1_2, 1 | (st.linear ? (1 << 5) | (1 << 6) : 0));
		ad(q + 14, GS_CLAMP_2, st.clamp ? 1 | (1 << 2) : 0);
	} else {
		ad(q + 10, GS_TEX0_2, 0);
		ad(q + 12, GS_TEX1_2, 1);
		ad(q + 14, GS_CLAMP_2, 0);
	}
	ad(q + 16, GS_TEXFLUSH, 0);

	switch (st.blend) {
	case DEMO_BLEND_ALPHA:		alpha = 0 | (1 << 2) | (0 << 4) | (1 << 6); break;
	case DEMO_BLEND_ADD:		alpha = 0 | (2 << 2) | (0 << 4) | (1 << 6); break;
	case DEMO_BLEND_MULTIPLY:	alpha = 1 | (2 << 2) | (0 << 4) | (2 << 6); break;
	default:			alpha = 0x44; break;
	}
	ad(q + 18, GS_ALPHA_2, alpha);
	ad(q + 20, GS_FOGCOL, (uint64_t)color_byte(st.fog_color[0]) |
		((uint64_t)color_byte(st.fog_color[1]) << 8) |
		((uint64_t)color_byte(st.fog_color[2]) << 16));
	/* Two spare slots keep the packet the size of the library's */
	ad(q + 22, GS_TEXFLUSH, 0);
	ad(q + 24, GS_TEXFLUSH, 0);
}

/*
 * The GS loses texture precision with large texture coordinates. Each
 * triangle gets its coordinates moved by a whole number, the smallest of its
 * corners, as the VU1 program of the library does.
 */
static void emit_triangle(const struct cv *a, const struct cv *b, const struct cv *c)
{
	const unsigned fa = clip_flags(a), fb = clip_flags(b), fc = clip_flags(c);
	struct cv poly[16];
	int n, i;

	if (fa & fb & fc)
		return;
	st_move[0] = (float)(int)fminf(a->s, fminf(b->s, c->s));
	st_move[1] = (float)(int)fminf(a->t, fminf(b->t, c->t));
	if (!(fa | fb | fc)) {
		out_vertex(a);
		out_vertex(b);
		out_vertex(c);
		return;
	}
	poly[0] = *a;
	poly[1] = *b;
	poly[2] = *c;
	n = clip_polygon(poly, 3, fa | fb | fc);
	for (i = 1; i + 1 < n; i++) {
		out_vertex(&poly[0]);
		out_vertex(&poly[i]);
		out_vertex(&poly[i + 1]);
	}
}

static void emit_line(const struct cv *a, const struct cv *b)
{
	const unsigned fa = clip_flags(a), fb = clip_flags(b);
	struct cv poly[16];
	int n;

	if (fa & fb)
		return;
	if (!(fa | fb)) {
		out_vertex(a);
		out_vertex(b);
		return;
	}
	poly[0] = *a;
	poly[1] = *b;
	n = clip_polygon(poly, 2, fa | fb);
	if (n >= 2) {
		out_vertex(&poly[0]);
		out_vertex(&poly[1]);
	}
}

void d_end(void)
{
	static struct cv cv[MAX_VERTICES];
	const int n = st.count;
	unsigned prim;
	int i;

	if (!n)
		return;
	for (i = 0; i < n; i++)
		transform(st.v[i], &cv[i]);

	switch (st.prim) {
	case DEMO_POINTS:	prim = 0; break;
	case DEMO_LINES:
	case DEMO_LINE_STRIP:	prim = 1; break;
	default:		prim = 3; break;
	}
	prim |= (st.smooth ? 1 << 3 : 0) | (st.texture ? 1 << 4 : 0) |
		(st.fog != DEMO_FOG_NONE ? 1 << 5 : 0) |
		(st.blend != DEMO_BLEND_NONE ? 1 << 6 : 0) | (1 << 9);

	apply_state();
	out_begin(prim);
	st_move[0] = st_move[1] = 0.0f;
	switch (st.prim) {
	case DEMO_POINTS:
		for (i = 0; i < n; i++)
			if (!clip_flags(&cv[i]))
				out_vertex(&cv[i]);
		break;
	case DEMO_LINES:
		for (i = 0; i + 1 < n; i += 2)
			emit_line(&cv[i], &cv[i + 1]);
		break;
	case DEMO_LINE_STRIP:
		for (i = 0; i + 1 < n; i++)
			emit_line(&cv[i], &cv[i + 1]);
		break;
	case DEMO_TRIANGLES:
		for (i = 0; i + 2 < n; i += 3)
			emit_triangle(&cv[i], &cv[i + 1], &cv[i + 2]);
		break;
	case DEMO_TRIANGLE_STRIP:
		for (i = 0; i + 2 < n; i++)
			emit_triangle(&cv[i], &cv[i + 1], &cv[i + 2]);
		break;
	case DEMO_TRIANGLE_FAN:
		for (i = 1; i + 1 < n; i++)
			emit_triangle(&cv[0], &cv[i], &cv[i + 1]);
		break;
	default:	/* Quads: two triangles each, as the library does */
		for (i = 0; i + 3 < n; i += 4) {
			emit_triangle(&cv[i], &cv[i + 1], &cv[i + 2]);
			emit_triangle(&cv[i], &cv[i + 2], &cv[i + 3]);
		}
		break;
	}
	out_end();
	st.count = 0;
}

/* --- display and keys -------------------------------------------------------------- */

static void show_frame(int index)
{
	uint64_t display = hw.display1;
	unsigned dw, dh, magh, magv;
	const char *env;

	set_reg(PS2GS_REG_DISPFB2, (uint64_t)hw.frame_page[index] |
		((uint64_t)hw.fbw << 9) | ((uint64_t)PSMCT32 << 15));
	if (!hw.have_display1)
		return;
	dw = (unsigned)((display >> 32) & 0xfff) + 1;
	dh = (unsigned)((display >> 44) & 0x7ff) + 1;
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
	display &= 0x7fffffull;
	display |= (uint64_t)(magh - 1) << 23;
	display |= (uint64_t)(magv - 1) << 27;
	display |= (uint64_t)(dw - 1) << 32;
	display |= (uint64_t)(dh - 1) << 44;
	set_reg(PS2GS_REG_DISPLAY2, display);
	set_reg(PS2GS_REG_PMODE, 0xff66);
	if (hw.verbose) {
		fprintf(stderr, "ps2demo-gs: display %dx%d, magnification %ux%u, window %ux%u\n",
			hw.width, hw.height, magh, magv, dw, dh);
		hw.verbose = 0;
	}
}

struct key_event {
	uint32_t sec, usec;
	uint16_t type, code;
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

int demo_key(int *code, int *pressed)
{
	struct key_event e;
	int i;

	for (i = 0; i < hw.keyboards; i++) {
		while (read(hw.keyboard[i], &e, sizeof(e)) == (ssize_t)sizeof(e)) {
			if (e.type == 1 && e.value != 2) {
				*code = e.code;
				*pressed = e.value;
				return 1;
			}
		}
	}
	return 0;
}

static void on_signal(int sig)
{
	demo_close();
	signal(sig, SIG_DFL);
	raise(sig);
}

int demo_open(void)
{
	static const int signals[] = { SIGINT, SIGTERM, SIGHUP, SIGSEGV, SIGBUS, SIGFPE, SIGABRT };
	struct ps2gs_info info;
	const char *env;
	int frame_pages;
	unsigned i;
	uint64_t *q;

	memset(&hw, 0, sizeof(hw));
#ifdef DEMO_FAKE
	hw.fd = -1;
	hw.tty = -1;
	hw.stream_bytes = STREAM_BYTES;
	hw.stream = malloc(hw.stream_bytes);
	hw.capacity = hw.stream_bytes / 4;
	hw.width = DEMO_WIDTH;
	hw.height = DEMO_HEIGHT;
	hw.fbw = (hw.width + 63) / 64;
	frame_pages = hw.fbw * ((hw.height + 31) / 32);
	hw.frame_page[1] = frame_pages;
	hw.zbuf_page = 2 * frame_pages;
	hw.pool_page = hw.next_page = 3 * frame_pages;
	ps2gl_model_init(hw.width, hw.height);
	goto settings;
#endif
	hw.fd = open("/dev/ps2gs", O_RDWR);
	if (hw.fd < 0) {
		fprintf(stderr, "ps2demo-gs: cannot open /dev/ps2gs: %s\n", strerror(errno));
		return -1;
	}
	if (ioctl(hw.fd, PS2GS_IOC_INFO, &info) < 0) {
		fprintf(stderr, "ps2demo-gs: /dev/ps2gs: %s\n", strerror(errno));
		return -1;
	}
	hw.stream_bytes = STREAM_BYTES;
	for (;;) {
		hw.stream = mmap(NULL, hw.stream_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, hw.fd, 0);
		if (hw.stream != MAP_FAILED || hw.stream_bytes <= 64 * 1024)
			break;
		hw.stream_bytes /= 2;
	}
	if (hw.stream == MAP_FAILED) {
		fprintf(stderr, "ps2demo-gs: mmap: %s\n", strerror(errno));
		return -1;
	}
	hw.capacity = hw.stream_bytes / 4;
	hw.width = DEMO_WIDTH;
	hw.height = DEMO_HEIGHT;
	env = getenv("PS2GL_SIZE");	/* The same option as the library: 640x480 */
	if (env && sscanf(env, "%dx%d", &hw.width, &hw.height) != 2) {
		hw.width = DEMO_WIDTH;
		hw.height = DEMO_HEIGHT;
	}
	if (hw.width < 64 || hw.width > 640 || hw.height < 64 || hw.height > 512) {
		hw.width = DEMO_WIDTH;
		hw.height = DEMO_HEIGHT;
	}
	hw.fbw = (hw.width + 63) / 64;
	frame_pages = hw.fbw * ((hw.height + 31) / 32);
	hw.frame_page[0] = 0;
	hw.frame_page[1] = frame_pages;
	hw.zbuf_page = 2 * frame_pages;
	hw.pool_page = hw.next_page = 3 * frame_pages;
	env = getenv("PS2GL_VERBOSE");
	hw.verbose = env && atoi(env) == 1;
	hw.have_display1 = get_reg(PS2GS_REG_DISPLAY1, &hw.display1) == 0;

	for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
		signal(signals[i], on_signal);
	hw.tty = open("/dev/tty1", O_RDWR);
	if (hw.tty >= 0 && ioctl(hw.tty, KDSETMODE, KD_GRAPHICS) == 0)
		hw.graphics_mode = 1;
	open_keyboards();

#ifdef DEMO_FAKE
settings:
#endif
	/* The settings that stay */
	q = packet_ad(7);
	if (!q)
		return -1;
	ad(q + 0, GS_PRMODECONT, 1);
	ad(q + 2, GS_TEXA, 0x80ull << 32);
	ad(q + 4, GS_COLCLAMP, 1);
	ad(q + 6, GS_DTHE, 0);
	ad(q + 8, GS_PABE, 0);
	ad(q + 10, GS_FBA_2, 0);
	ad(q + 12, GS_DIMX, 0);

	memset(&st, 0, sizeof(st));
	m_identity(st.projection);
	m_identity(st.modelview);
	m_identity(st.clip);
	st.color_mask = 1;
	st.depth_test = 1;
	st.depth_write = 1;
	st.smooth = 1;
	st.color[0] = st.color[1] = st.color[2] = st.color[3] = 1.0f;

	d_clear(0, 0, 0);
	send(1);
	show_frame(0);
	hw.draw = 1;
	d_clear(0, 0, 0);
	send(1);
	return 0;
}

void demo_swap(void)
{
	send(1);
	if (hw.fd >= 0 && demo_vsync)
		ioctl(hw.fd, PS2GS_IOC_VSYNC);
	show_frame(hw.draw);
	hw.draw ^= 1;
}

void demo_close(void)
{
	int i;

#ifdef DEMO_FAKE
	send(1);
	return;
#endif
	if (hw.fd < 0)
		return;
	send(1);
	ioctl(hw.fd, PS2GS_IOC_WAIT, PS2GS_WAIT_GIF);
	for (i = 0; i < hw.keyboards; i++) {
		ioctl(hw.keyboard[i], GRAB_EVENTS, 0);
		close(hw.keyboard[i]);
	}
	hw.keyboards = 0;
	munmap(hw.stream, hw.stream_bytes);
	close(hw.fd);
	hw.fd = -1;
	if (hw.graphics_mode && hw.tty >= 0)
		ioctl(hw.tty, KDSETMODE, KD_TEXT);
	if (hw.tty >= 0)
		close(hw.tty);
	hw.tty = -1;
}

int main(int argc, char **argv)
{
	return demo_main(argc, argv, "ps2demo-gs");
}
