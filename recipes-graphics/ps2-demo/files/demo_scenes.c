// SPDX-License-Identifier: MIT
/*
 * The demo scenes. Each scene shows a set of features of the library on
 * the GS, and turns with the frame number. The same code runs on both
 * back ends (demo_gl.c and demo_gs.c).
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "demo.h"

/* --- math ------------------------------------------------------------------ */

void m_identity(float m[16])
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void m_multiply(float out[16], const float a[16], const float b[16])
{
	float r[16];
	int i, j, k;

	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++) {
			float s = 0.0f;

			for (k = 0; k < 4; k++)
				s += a[4 * k + j] * b[4 * i + k];
			r[4 * i + j] = s;
		}
	memcpy(out, r, sizeof(r));
}

void m_translate(float m[16], float x, float y, float z)
{
	float t[16];

	m_identity(t);
	t[12] = x;
	t[13] = y;
	t[14] = z;
	m_multiply(m, m, t);
}

void m_rotate_y(float m[16], float degrees)
{
	const float a = degrees * (float)M_PI / 180.0f, c = cosf(a), s = sinf(a);
	float r[16];

	m_identity(r);
	r[0] = c;
	r[2] = -s;
	r[8] = s;
	r[10] = c;
	m_multiply(m, m, r);
}

void m_rotate_x(float m[16], float degrees)
{
	const float a = degrees * (float)M_PI / 180.0f, c = cosf(a), s = sinf(a);
	float r[16];

	m_identity(r);
	r[5] = c;
	r[6] = s;
	r[9] = -s;
	r[10] = c;
	m_multiply(m, m, r);
}

void m_frustum(float m[16], float l, float r, float b, float t, float n, float f)
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = 2.0f * n / (r - l);
	m[5] = 2.0f * n / (t - b);
	m[8] = (r + l) / (r - l);
	m[9] = (t + b) / (t - b);
	m[10] = -(f + n) / (f - n);
	m[11] = -1.0f;
	m[14] = -2.0f * f * n / (f - n);
}

void m_ortho(float m[16], float l, float r, float b, float t, float n, float f)
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = 2.0f / (r - l);
	m[5] = 2.0f / (t - b);
	m[10] = -2.0f / (f - n);
	m[12] = -(r + l) / (r - l);
	m[13] = -(t + b) / (t - b);
	m[14] = -(f + n) / (f - n);
	m[15] = 1.0f;
}

/* --- textures --------------------------------------------------------------- */

static uint8_t pixels[128 * 128 * 4];

void demo_make_textures(void)
{
	int x, y;

	/* Checker: 64x64, blue and yellow, 8 pixel squares */
	for (y = 0; y < 64; y++)
		for (x = 0; x < 64; x++) {
			uint8_t *p = &pixels[4 * (64 * y + x)];
			const int on = ((x >> 3) ^ (y >> 3)) & 1;

			p[0] = on ? 240 : 40;
			p[1] = on ? 200 : 60;
			p[2] = on ? 40 : 220;
			p[3] = 255;
		}
	d_texture_create(DEMO_TEX_CHECKER, 64, 64, pixels);

	/* Gradient: 128x32, red to green along x, blue along y */
	for (y = 0; y < 32; y++)
		for (x = 0; x < 128; x++) {
			uint8_t *p = &pixels[4 * (128 * y + x)];

			p[0] = 255 - 2 * x;
			p[1] = 2 * x;
			p[2] = 8 * y;
			p[3] = 255;
		}
	d_texture_create(DEMO_TEX_GRADIENT, 128, 32, pixels);

	/* Alpha: 64x64 white, a round hole with alpha 0 outside a disc */
	for (y = 0; y < 64; y++)
		for (x = 0; x < 64; x++) {
			uint8_t *p = &pixels[4 * (64 * y + x)];
			const int dx = x - 32, dy = y - 32;
			const int inside = dx * dx + dy * dy < 26 * 26;

			p[0] = 255;
			p[1] = 255;
			p[2] = 255;
			p[3] = inside ? 255 : 0;
		}
	d_texture_create(DEMO_TEX_ALPHA, 64, 64, pixels);

	/* Light: 64x64 gray, bright in the middle, dark at the edge */
	for (y = 0; y < 64; y++)
		for (x = 0; x < 64; x++) {
			uint8_t *p = &pixels[4 * (64 * y + x)];
			const float dx = (x - 31.5f) / 32.0f, dy = (y - 31.5f) / 32.0f;
			float v = 1.0f - sqrtf(dx * dx + dy * dy);
			uint8_t g;

			if (v < 0.1f)
				v = 0.1f;
			g = (uint8_t)(v * 255.0f);
			p[0] = g;
			p[1] = g;
			p[2] = g;
			p[3] = 255;
		}
	d_texture_create(DEMO_TEX_LIGHT, 64, 64, pixels);

	/* Brick: 64x64 */
	for (y = 0; y < 64; y++)
		for (x = 0; x < 64; x++) {
			uint8_t *p = &pixels[4 * (64 * y + x)];
			const int row = y / 16, shift = (row & 1) * 16;
			const int bx = (x + shift) % 32, by = y % 16;
			const int mortar = bx < 2 || by < 2;

			p[0] = mortar ? 180 : 170 + ((x * 7 + y * 3) % 40);
			p[1] = mortar ? 180 : 70 + ((x * 5 + y * 11) % 30);
			p[2] = mortar ? 180 : 50;
			p[3] = 255;
		}
	d_texture_create(DEMO_TEX_BRICK, 64, 64, pixels);

	/*
	 * Sizes: 64x64, 128x128, 256x256 and 512x256, at 16 bits (alpha 255)
	 * and at 32 bits (alpha 200, which the library keeps only in 32 bits).
	 * Each has 8 by 8 checks, a diagonal and a white frame, so a wrong
	 * size, stride or half shows at once.
	 */
	{
		static const int sizes[4][2] = { { 64, 64 }, { 128, 128 }, { 256, 256 }, { 512, 256 } };
		static uint8_t big[512 * 256 * 4];
		int k, bits;

		for (bits = 0; bits < 2; bits++)
			for (k = 0; k < 4; k++) {
				const int w = sizes[k][0], h = sizes[k][1];

				for (y = 0; y < h; y++)
					for (x = 0; x < w; x++) {
						uint8_t *p = &big[4 * (w * y + x)];
						const int on = ((x * 8 / w) ^ (y * 8 / h)) & 1;
						const int frame = x < 2 || y < 2 || x >= w - 2 || y >= h - 2;
						const int diagonal = x * h / w == y || x * h / w == y + 1;

						p[0] = frame ? 255 : diagonal ? 255 : on ? 60 : 200;
						p[1] = frame ? 255 : diagonal ? 0 : on ? 120 : 200;
						p[2] = frame ? 255 : diagonal ? 0 : on ? 220 : 60;
						p[3] = bits ? 200 : 255;
					}
				d_texture_create(DEMO_TEX_SIZES + 4 * bits + k, w, h, big);
			}

		/*
		 * Many: 24 textures of 256x256 at 16 bits, 3 MiB in all, more than
		 * the GS memory holds next to the frame buffers. Each has its
		 * number in binary as white bars (bit 0 at the left), on a
		 * gradient with its own hue. Smooth content: the minified
		 * sampling then gives the same picture on all back ends.
		 */
		for (k = 0; k < DEMO_TEX_MANY_COUNT; k++) {
			for (y = 0; y < 256; y++)
				for (x = 0; x < 256; x++) {
					uint8_t *p = &big[4 * (256 * y + x)];
					const int bit = x / 48, in_bar = x % 48 < 40 && bit < 5;
					const int bar = y > 180 && y < 230 && in_bar && (k >> bit) & 1;

					p[0] = bar ? 255 : (10 * k + x) & 255;
					p[1] = bar ? 255 : (255 - 10 * k + y) & 255;
					p[2] = bar ? 255 : 128;
					p[3] = 255;
				}
			d_texture_create(DEMO_TEX_MANY + k, 256, 256, big);
		}

		/* The lightmap: dark gray, with a brighter band */
		for (y = 0; y < 128; y++)
			for (x = 0; x < 128; x++) {
				uint8_t *p = &big[4 * (128 * y + x)];

				p[0] = p[1] = p[2] = y > 56 && y < 72 ? 128 : 48;
				p[3] = 255;
			}
		d_texture_create(DEMO_TEX_LIGHTMAP, 128, 128, big);
	}
}

/* --- helpers ------------------------------------------------------------------ */

static void view(int frame, float distance, float height, float speed)
{
	float m[16];

	m_identity(m);
	m_translate(m, 0.0f, -height, -distance);
	m_rotate_x(m, 15.0f);
	m_rotate_y(m, frame * speed);
	d_modelview(m);
}

static void perspective(void)
{
	d_frustum(-1.0f, 1.0f, -0.75f, 0.75f, 1.0f, 100.0f);
}

/* A cube of size 2 at the origin, each face with its own color */
static void cube(int textured)
{
	static const float faces[6][4][3] = {
		{ { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } },
		{ { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 } },
		{ { 1, -1, 1 }, { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 } },
		{ { -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 } },
		{ { -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 }, { -1, 1, -1 } },
		{ { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 }, { -1, -1, 1 } },
	};
	static const float colors[6][3] = {
		{ 1, 0.3f, 0.3f }, { 0.3f, 1, 0.3f }, { 0.3f, 0.3f, 1 },
		{ 1, 1, 0.3f }, { 1, 0.3f, 1 }, { 0.3f, 1, 1 },
	};
	static const float st[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	int f, v;

	d_begin(DEMO_QUADS);
	for (f = 0; f < 6; f++) {
		if (textured)
			d_color(1, 1, 1, 1);
		else
			d_color(colors[f][0], colors[f][1], colors[f][2], 1);
		for (v = 0; v < 4; v++) {
			d_texcoord(st[v][0], st[v][1]);
			d_vertex(faces[f][v][0], faces[f][v][1], faces[f][v][2]);
		}
	}
	d_end();
}

/* A floor of pieces from -size to size at y, with texture repeats */
static void floor_pieces(float size, float y, float repeat)
{
	const int n = 8;
	int i, j;

	d_begin(DEMO_QUADS);
	for (j = 0; j < n; j++)
		for (i = 0; i < n; i++) {
			const float x0 = -size + 2 * size * i / n, x1 = x0 + 2 * size / n;
			const float z0 = -size + 2 * size * j / n, z1 = z0 + 2 * size / n;
			const float s0 = repeat * i / n, s1 = repeat * (i + 1) / n;
			const float t0 = repeat * j / n, t1 = repeat * (j + 1) / n;

			d_texcoord(s0, t0); d_vertex(x0, y, z0);
			d_texcoord(s1, t0); d_vertex(x1, y, z0);
			d_texcoord(s1, t1); d_vertex(x1, y, z1);
			d_texcoord(s0, t1); d_vertex(x0, y, z1);
		}
	d_end();
}

static void reset_state(void)
{
	d_depth(1, 1);
	d_blend(DEMO_BLEND_NONE);
	d_alpha_test(0, 0.0f);
	d_fog(DEMO_FOG_NONE, 0, 0, 0, NULL);
	d_color_mask(1);
	d_texture(0, 0, 0);
	d_smooth(1);
	d_color(1, 1, 1, 1);
}

/* --- scenes ------------------------------------------------------------------- */

/* 1. Colors: a smooth cube, a flat cube, a fan and a strip, with the depth test */
static void scene_colors(int frame)
{
	float m[16];
	int i;

	d_clear(0.1f, 0.1f, 0.2f);
	perspective();
	view(frame, 8.0f, 1.0f, 1.0f);
	reset_state();

	/* Smooth: each corner a different color through the vertex color */
	d_begin(DEMO_TRIANGLES);
	d_color(1, 0, 0, 1); d_vertex(-3, 0, -2);
	d_color(0, 1, 0, 1); d_vertex(-1, 0, -2);
	d_color(0, 0, 1, 1); d_vertex(-2, 2, -2);
	d_end();

	/* Flat: the last vertex gives the color */
	d_smooth(0);
	d_begin(DEMO_TRIANGLES);
	d_color(1, 0, 0, 1); d_vertex(1, 0, -2);
	d_color(0, 1, 0, 1); d_vertex(3, 0, -2);
	d_color(1, 1, 0, 1); d_vertex(2, 2, -2);
	d_end();
	d_smooth(1);

	/* A fan: a wheel on the floor */
	d_begin(DEMO_TRIANGLE_FAN);
	d_color(1, 1, 1, 1);
	d_vertex(0, -1, 0);
	for (i = 0; i <= 12; i++) {
		const float a = i * 2.0f * (float)M_PI / 12.0f;

		d_color(0.5f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a), 0.5f, 1);
		d_vertex(3.0f * cosf(a), -1, 3.0f * sinf(a));
	}
	d_end();

	/* A strip: a ribbon that goes through the fan (depth test) */
	d_begin(DEMO_TRIANGLE_STRIP);
	for (i = 0; i <= 8; i++) {
		const float x = -4.0f + i;

		d_color(0.2f, 0.8f, 1.0f, 1); d_vertex(x, -1.5f + 0.4f * i, 0.5f);
		d_color(1.0f, 0.8f, 0.2f, 1); d_vertex(x, -1.5f + 0.4f * i, -0.5f);
	}
	d_end();

	/* Two cubes that turn the other way */
	m_identity(m);
	m_translate(m, 0.0f, -1.0f, -8.0f);
	m_rotate_x(m, 15.0f);
	m_rotate_y(m, frame);
	m_translate(m, 0.0f, 2.5f, 0.0f);
	m_rotate_y(m, -3.0f * frame);
	m_rotate_x(m, 2.0f * frame);
	d_modelview(m);
	cube(0);
}

/* 2. Textures: nearest and linear filters, clamp and repeat */
static void scene_textures(int frame)
{
	float m[16];

	d_clear(0.2f, 0.1f, 0.1f);
	perspective();
	reset_state();

	/* The floor: checker with repeat, nearest */
	view(frame, 9.0f, 1.5f, 0.7f);
	d_texture(DEMO_TEX_CHECKER, 0, 0);
	floor_pieces(6.0f, -1.5f, 6.0f);

	/* A gradient with the linear filter and clamp: the edges stretch */
	d_texture(DEMO_TEX_GRADIENT, 1, 1);
	d_begin(DEMO_QUADS);
	d_texcoord(-0.5f, -0.5f); d_vertex(-5, 0, -4);
	d_texcoord(1.5f, -0.5f); d_vertex(-1, 0, -4);
	d_texcoord(1.5f, 1.5f); d_vertex(-1, 2, -4);
	d_texcoord(-0.5f, 1.5f); d_vertex(-5, 2, -4);
	d_end();

	/* The same with repeat and nearest */
	d_texture(DEMO_TEX_GRADIENT, 0, 0);
	d_begin(DEMO_QUADS);
	d_texcoord(-0.5f, -0.5f); d_vertex(1, 0, -4);
	d_texcoord(1.5f, -0.5f); d_vertex(5, 0, -4);
	d_texcoord(1.5f, 1.5f); d_vertex(5, 2, -4);
	d_texcoord(-0.5f, 1.5f); d_vertex(1, 2, -4);
	d_end();

	/* A textured cube with the brick texture, linear, with a tint */
	m_identity(m);
	m_translate(m, 0.0f, -1.5f, -9.0f);
	m_rotate_x(m, 15.0f);
	m_rotate_y(m, 0.7f * frame);
	m_translate(m, 0.0f, 1.5f, 0.0f);
	m_rotate_y(m, 2.0f * frame);
	m_rotate_x(m, frame);
	d_modelview(m);
	d_texture(DEMO_TEX_BRICK, 1, 0);
	d_begin(DEMO_QUADS);
	{
		int f, v;
		static const float faces[6][4][3] = {
			{ { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } },
			{ { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 } },
			{ { 1, -1, 1 }, { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 } },
			{ { -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 } },
			{ { -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 }, { -1, 1, -1 } },
			{ { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 }, { -1, -1, 1 } },
		};
		static const float st[4][2] = { { 0, 0 }, { 2, 0 }, { 2, 2 }, { 0, 2 } };

		for (f = 0; f < 6; f++) {
			d_color(1, 1, 1, 1);
			for (v = 0; v < 4; v++) {
				/* The vertex color shades the faces */
				d_color(0.6f + 0.4f * (f & 1), 0.6f + 0.2f * f / 5.0f, 0.9f, 1);
				d_texcoord(st[v][0], st[v][1]);
				d_vertex(faces[f][v][0], faces[f][v][1], faces[f][v][2]);
			}
		}
	}
	d_end();
}

/* 3. Blending: alpha, additive, and the alpha test */
static void scene_blend(int frame)
{
	int i;

	d_clear(0.1f, 0.2f, 0.1f);
	perspective();
	reset_state();
	view(frame, 9.0f, 1.5f, 0.8f);

	d_texture(DEMO_TEX_CHECKER, 0, 0);
	floor_pieces(6.0f, -1.5f, 4.0f);
	d_texture(0, 0, 0);

	/* Alpha blended panes: red, green, blue at half alpha, overlapping */
	d_blend(DEMO_BLEND_ALPHA);
	d_depth(1, 0);		/* Test, no write: the order does not matter */
	for (i = 0; i < 3; i++) {
		const float a = (i * 120.0f + frame) * (float)M_PI / 180.0f;
		const float x = 2.0f * cosf(a), z = 2.0f * sinf(a);

		d_color(i == 0, i == 1, i == 2, 0.5f);
		d_begin(DEMO_QUADS);
		d_vertex(x - 1.5f, -1.5f, z); d_vertex(x + 1.5f, -1.5f, z);
		d_vertex(x + 1.5f, 1.5f, z); d_vertex(x - 1.5f, 1.5f, z);
		d_end();
	}

	/* An additive glow in the middle: a fan from bright to black */
	d_blend(DEMO_BLEND_ADD);
	d_begin(DEMO_TRIANGLE_FAN);
	d_color(1, 1, 0.6f, 1);
	d_vertex(0, 0.5f, 0);
	for (i = 0; i <= 16; i++) {
		const float a = i * 2.0f * (float)M_PI / 16.0f;

		d_color(0.6f, 0.2f, 0, 1);
		d_vertex(2.0f * cosf(a), 0.5f + 2.0f * sinf(a), 0);
	}
	d_end();
	d_depth(1, 1);

	/* Alpha test: the disc texture cuts a round shape from a quad */
	d_blend(DEMO_BLEND_NONE);
	d_alpha_test(1, 0.5f);
	d_texture(DEMO_TEX_ALPHA, 0, 1);
	d_color(1, 0.5f, 0.2f, 1);
	d_begin(DEMO_QUADS);
	d_texcoord(0, 0); d_vertex(-5, -1.5f, 3);
	d_texcoord(1, 0); d_vertex(-2, -1.5f, 3);
	d_texcoord(1, 1); d_vertex(-2, 1.5f, 3);
	d_texcoord(0, 1); d_vertex(-5, 1.5f, 3);
	d_end();
	/* The same without the test: the hole shows the color */
	d_alpha_test(0, 0.0f);
	d_begin(DEMO_QUADS);
	d_texcoord(0, 0); d_vertex(2, -1.5f, 3);
	d_texcoord(1, 0); d_vertex(5, -1.5f, 3);
	d_texcoord(1, 1); d_vertex(5, 1.5f, 3);
	d_texcoord(0, 1); d_vertex(2, 1.5f, 3);
	d_end();
}

/* 4. Multiply: a lightmap over the brick floor, as Quake does */
static void scene_multiply(int frame)
{
	int i;

	d_clear(0.0f, 0.0f, 0.0f);
	perspective();
	reset_state();
	view(frame, 10.0f, 2.0f, 0.6f);

	d_texture(DEMO_TEX_BRICK, 1, 0);
	floor_pieces(6.0f, -1.5f, 8.0f);

	/* Lights that move over the floor: the texture multiplies the frame */
	d_blend(DEMO_BLEND_MULTIPLY);
	d_depth(1, 0);
	d_texture(DEMO_TEX_LIGHT, 1, 1);
	d_color(1, 1, 1, 1);
	for (i = 0; i < 3; i++) {
		const float a = (i * 120.0f + 2.0f * frame) * (float)M_PI / 180.0f;
		const float x = 3.0f * cosf(a), z = 3.0f * sinf(a);

		d_begin(DEMO_QUADS);
		d_texcoord(0, 0); d_vertex(x - 2.5f, -1.49f, z - 2.5f);
		d_texcoord(1, 0); d_vertex(x + 2.5f, -1.49f, z - 2.5f);
		d_texcoord(1, 1); d_vertex(x + 2.5f, -1.49f, z + 2.5f);
		d_texcoord(0, 1); d_vertex(x - 2.5f, -1.49f, z + 2.5f);
		d_end();
	}
	d_depth(1, 1);
	d_blend(DEMO_BLEND_NONE);

	/* A wall with the checker, lit by the same trick */
	d_texture(DEMO_TEX_CHECKER, 0, 0);
	d_begin(DEMO_QUADS);
	d_texcoord(0, 0); d_vertex(-6, -1.5f, -6);
	d_texcoord(4, 0); d_vertex(6, -1.5f, -6);
	d_texcoord(4, 1); d_vertex(6, 1.5f, -6);
	d_texcoord(0, 1); d_vertex(-6, 1.5f, -6);
	d_end();
	d_blend(DEMO_BLEND_MULTIPLY);
	d_depth(1, 0);
	d_texture(DEMO_TEX_LIGHT, 1, 1);
	d_begin(DEMO_QUADS);
	d_texcoord(0, 0); d_vertex(-6, -1.5f, -5.99f);
	d_texcoord(1, 0); d_vertex(6, -1.5f, -5.99f);
	d_texcoord(1, 1); d_vertex(6, 1.5f, -5.99f);
	d_texcoord(0, 1); d_vertex(-6, 1.5f, -5.99f);
	d_end();
}

/* 5. Fog: pillars in the distance, exp fog then linear fog */
static void scene_fog(int frame)
{
	static const float gray[3] = { 0.5f, 0.55f, 0.6f };
	const int linear = (frame / 300) & 1;
	float m[16];
	int i;

	d_clear(gray[0], gray[1], gray[2]);
	perspective();
	reset_state();
	if (linear)
		d_fog(DEMO_FOG_LINEAR, 0, 5.0f, 30.0f, gray);
	else
		d_fog(DEMO_FOG_EXP, 0.08f, 0, 0, gray);

	/* The camera moves forward along the pillars and comes back */
	m_identity(m);
	m_translate(m, 0.0f, -1.0f, -4.0f + 10.0f * sinf(frame * 0.01f));
	d_modelview(m);

	d_texture(DEMO_TEX_CHECKER, 0, 0);
	d_begin(DEMO_QUADS);
	d_texcoord(0, 0); d_vertex(-4, -1, 10);
	d_texcoord(4, 0); d_vertex(4, -1, 10);
	d_texcoord(4, 20); d_vertex(4, -1, -40);
	d_texcoord(0, 20); d_vertex(-4, -1, -40);
	d_end();

	d_texture(DEMO_TEX_BRICK, 1, 0);
	for (i = 0; i < 10; i++) {
		const float z = 5.0f - 5.0f * i;

		d_begin(DEMO_QUADS);
		d_color(1, 1, 1, 1);
		d_texcoord(0, 0); d_vertex(-4, -1, z); d_texcoord(1, 0); d_vertex(-3, -1, z);
		d_texcoord(1, 2); d_vertex(-3, 2, z); d_texcoord(0, 2); d_vertex(-4, 2, z);
		d_texcoord(0, 0); d_vertex(3, -1, z); d_texcoord(1, 0); d_vertex(4, -1, z);
		d_texcoord(1, 2); d_vertex(4, 2, z); d_texcoord(0, 2); d_vertex(3, 2, z);
		d_end();
	}
	d_texture(0, 0, 0);
	d_color(1, 0.3f, 0.2f, 1);
	d_begin(DEMO_QUADS);
	d_vertex(-1, -1, -44); d_vertex(1, -1, -44); d_vertex(1, 2, -44); d_vertex(-1, 2, -44);
	d_end();
}

/* 6. Clipping: a floor through the camera, a wall through the near plane,
 * lines and points across the edges */
static void scene_clipping(int frame)
{
	float m[16];
	int i;

	d_clear(0.3f, 0.4f, 0.6f);
	perspective();
	reset_state();
	m_identity(m);
	m_translate(m, 0.0f, -1.0f, 0.0f);
	m_rotate_y(m, frame * 0.5f);
	d_modelview(m);

	d_texture(DEMO_TEX_CHECKER, 0, 0);
	d_begin(DEMO_QUADS);
	d_texcoord(0, 0); d_vertex(-100, -1, 100);
	d_texcoord(40, 0); d_vertex(100, -1, 100);
	d_texcoord(40, 40); d_vertex(100, -1, -100);
	d_texcoord(0, 40); d_vertex(-100, -1, -100);
	d_end();
	d_texture(0, 0, 0);

	/* Walls around the camera, close enough to cross the near plane */
	for (i = 0; i < 4; i++) {
		const float a = i * 0.5f * (float)M_PI, c = cosf(a), s = sinf(a);

		d_color(0.5f + 0.5f * (i == 0), 0.5f + 0.5f * (i == 1), 0.5f + 0.5f * (i == 2), 1);
		d_begin(DEMO_TRIANGLE_STRIP);
		d_vertex(3 * c - 1.5f * s, -1, 3 * s + 1.5f * c);
		d_vertex(3 * c - 1.5f * s, 1, 3 * s + 1.5f * c);
		d_vertex(3 * c + 1.5f * s, -1, 3 * s - 1.5f * c);
		d_vertex(3 * c + 1.5f * s, 1, 3 * s - 1.5f * c);
		d_end();
	}

	/* Lines: a star on the floor, and a frame in the air */
	d_color(1, 1, 0, 1);
	d_begin(DEMO_LINES);
	for (i = 0; i < 8; i++) {
		const float a = i * 0.25f * (float)M_PI;

		d_vertex(0, -0.9f, 0);
		d_vertex(8.0f * cosf(a), -0.9f, 8.0f * sinf(a));
	}
	d_end();
	d_color(0, 0, 0, 1);
	d_begin(DEMO_LINE_STRIP);
	d_vertex(-6, 2, -6); d_vertex(6, 2, -6); d_vertex(6, 2, 6);
	d_vertex(-6, 2, 6); d_vertex(-6, 2, -6);
	d_end();

	/* Points in a ring, half of them behind the camera at any time */
	d_color(1, 1, 1, 1);
	d_begin(DEMO_POINTS);
	for (i = 0; i < 36; i++) {
		const float a = i * 10.0f * (float)M_PI / 180.0f;

		d_vertex(1.5f * cosf(a), 1.5f, 1.5f * sinf(a));
	}
	d_end();
}

/* 7. Color mask and the depth buffer: a shape drawn into depth only */
static void scene_mask(int frame)
{
	d_clear(0.5f, 0.5f, 0.5f);
	perspective();
	reset_state();
	view(frame, 8.0f, 0.0f, 1.0f);

	/* A cube written to the depth buffer only: it hides what is behind */
	d_color_mask(0);
	cube(0);
	d_color_mask(1);

	/* A textured wall behind: the cube cuts a hole in it */
	d_texture(DEMO_TEX_GRADIENT, 1, 0);
	d_begin(DEMO_QUADS);
	d_texcoord(0, 0); d_vertex(-4, -3, -2.5f);
	d_texcoord(1, 0); d_vertex(4, -3, -2.5f);
	d_texcoord(1, 1); d_vertex(4, 3, -2.5f);
	d_texcoord(0, 1); d_vertex(-4, 3, -2.5f);
	d_end();
	d_texture(0, 0, 0);

	/* A ring of small quads in front, no depth test: always visible */
	d_depth(0, 0);
	d_color(1, 0.2f, 0.2f, 1);
	d_begin(DEMO_QUADS);
	d_vertex(-0.3f, 2.5f, 0); d_vertex(0.3f, 2.5f, 0); d_vertex(0.3f, 3, 0); d_vertex(-0.3f, 3, 0);
	d_end();
}

/* 8. Texture sizes: 16-bit above, 32-bit below, 64 to 512 wide, left to right */
static void scene_sizes(int frame)
{
	int k, bits;

	(void)frame;
	d_clear(0.1f, 0.1f, 0.1f);
	d_ortho(0.0f, 320.0f, 240.0f, 0.0f, -1.0f, 1.0f);
	{
		float m[16];

		m_identity(m);
		d_modelview(m);
	}
	reset_state();
	d_depth(0, 0);
	d_color(1, 1, 1, 1);
	for (bits = 0; bits < 2; bits++)
		for (k = 0; k < 4; k++) {
			const float x0 = 8.0f + 78.0f * k, y0 = 20.0f + 110.0f * bits;
			const float w = k == 3 ? 70.0f : 70.0f, h = k == 3 ? 35.0f : 70.0f;

			d_texture(DEMO_TEX_SIZES + 4 * bits + k, 0, 1);
			d_begin(DEMO_QUADS);
			d_texcoord(0, 0); d_vertex(x0, y0, 0);
			d_texcoord(1, 0); d_vertex(x0 + w, y0, 0);
			d_texcoord(1, 1); d_vertex(x0 + w, y0 + h, 0);
			d_texcoord(0, 1); d_vertex(x0, y0 + h, 0);
			d_end();
		}
	d_texture(0, 0, 0);
}

/* 9. Many textures: all 24 each frame, so the library has to swap them in and out */
static void scene_many(int frame)
{
	int k;

	d_clear(0.1f, 0.1f, 0.1f);
	d_ortho(0.0f, 320.0f, 240.0f, 0.0f, -1.0f, 1.0f);
	{
		float m[16];

		m_identity(m);
		d_modelview(m);
	}
	reset_state();
	d_depth(0, 0);
	d_color(1, 1, 1, 1);
	for (k = 0; k < DEMO_TEX_MANY_COUNT; k++) {
		/* The order changes with time, so the swapping pattern changes too */
		const int i = (k + frame / 50) % DEMO_TEX_MANY_COUNT;
		const float x0 = 4.0f + 53.0f * (k % 6), y0 = 4.0f + 59.0f * (k / 6);

		d_texture(DEMO_TEX_MANY + i, 0, 1);
		d_begin(DEMO_QUADS);
		d_texcoord(0, 0); d_vertex(x0, y0, 0);
		d_texcoord(1, 0); d_vertex(x0 + 50, y0, 0);
		d_texcoord(1, 1); d_vertex(x0 + 50, y0 + 55, 0);
		d_texcoord(0, 1); d_vertex(x0, y0 + 55, 0);
		d_end();
	}
	d_texture(0, 0, 0);
}

/*
 * 10. Lightmap: the brick wall and floor, multiplied by a 128x128 light
 * texture of which a part changes each frame, as the lightmaps of Quake do.
 * The texture starts dark. A bright spot moves over it. In the frame
 * before, the spot was elsewhere: the update must take, or the spot stays.
 */
static void scene_lightmap(int frame)
{
	static uint8_t patch[32 * 32 * 4];
	static int last_x = -1, last_y = -1;
	const int px = 8 + (int)(44.0f + 44.0f * cosf(frame * 0.05f));
	const int py = 8 + (int)(44.0f + 44.0f * sinf(frame * 0.037f));
	int x, y;

	/* Dark again where the spot was */
	if (last_x >= 0) {
		memset(patch, 0, sizeof(patch));
		for (y = 0; y < 32 * 32; y++) {
			patch[4 * y + 0] = patch[4 * y + 1] = patch[4 * y + 2] = 48;
			patch[4 * y + 3] = 255;
		}
		d_texture_update(DEMO_TEX_LIGHTMAP, last_x, last_y, 32, 32, patch);
	}
	for (y = 0; y < 32; y++)
		for (x = 0; x < 32; x++) {
			uint8_t *p = &patch[4 * (32 * y + x)];
			const float dx = x - 15.5f, dy = y - 15.5f;
			float v = 1.0f - sqrtf(dx * dx + dy * dy) / 16.0f;

			if (v < 0.0f)
				v = 0.0f;
			p[0] = p[1] = p[2] = 48 + (uint8_t)(207.0f * v);
			p[3] = 255;
		}
	d_texture_update(DEMO_TEX_LIGHTMAP, px, py, 32, 32, patch);
	last_x = px;
	last_y = py;

	d_clear(0.0f, 0.0f, 0.0f);
	perspective();
	reset_state();
	view(frame, 10.0f, 2.0f, 0.6f);

	d_texture(DEMO_TEX_BRICK, 1, 0);
	floor_pieces(6.0f, -1.5f, 8.0f);
	d_begin(DEMO_QUADS);
	d_texcoord(0, 0); d_vertex(-6, -1.5f, -6);
	d_texcoord(4, 0); d_vertex(6, -1.5f, -6);
	d_texcoord(4, 1); d_vertex(6, 1.5f, -6);
	d_texcoord(0, 1); d_vertex(-6, 1.5f, -6);
	d_end();

	d_blend(DEMO_BLEND_MULTIPLY);
	d_depth(1, 0);
	d_texture(DEMO_TEX_LIGHTMAP, 1, 1);
	d_color(1, 1, 1, 1);
	d_begin(DEMO_QUADS);
	d_texcoord(0, 0); d_vertex(-6, -1.49f, -6);
	d_texcoord(1, 0); d_vertex(6, -1.49f, -6);
	d_texcoord(1, 1); d_vertex(6, -1.49f, 6);
	d_texcoord(0, 1); d_vertex(-6, -1.49f, 6);
	d_end();
	d_begin(DEMO_QUADS);
	d_texcoord(0, 0); d_vertex(-6, -1.5f, -5.99f);
	d_texcoord(1, 0); d_vertex(6, -1.5f, -5.99f);
	d_texcoord(1, 1); d_vertex(6, 1.5f, -5.99f);
	d_texcoord(0, 1); d_vertex(-6, 1.5f, -5.99f);
	d_end();
	d_depth(1, 1);
	d_blend(DEMO_BLEND_NONE);
}

static const struct {
	const char *name;
	void (*draw)(int frame);
} scenes[] = {
	{ "colors", scene_colors },
	{ "textures", scene_textures },
	{ "blend", scene_blend },
	{ "multiply", scene_multiply },
	{ "fog", scene_fog },
	{ "clipping", scene_clipping },
	{ "mask", scene_mask },
	{ "sizes", scene_sizes },
	{ "many", scene_many },
	{ "lightmap", scene_lightmap },
};

int demo_scene_count(void)
{
	return sizeof(scenes) / sizeof(scenes[0]);
}

const char *demo_scene_name(int scene)
{
	return scenes[scene].name;
}

void demo_scene_draw(int scene, int frame)
{
	scenes[scene].draw(frame);
}
