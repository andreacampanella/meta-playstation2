// SPDX-License-Identifier: MIT
/*
 * The demo scenes draw through this small interface. Two back ends exist:
 *
 *   demo_gl.c  OpenGL through libGL.so.1 (VU1 and the GS)
 *   demo_gs.c  the GS directly, with the transform, the clipping and the
 *              fog on the CPU
 *
 * Both must show the same picture. The scene code is in demo_scenes.c.
 */
#ifndef DEMO_H
#define DEMO_H

#include <stdint.h>

#define DEMO_WIDTH	320
#define DEMO_HEIGHT	240

enum demo_prim {
	DEMO_POINTS, DEMO_LINES, DEMO_LINE_STRIP,
	DEMO_TRIANGLES, DEMO_TRIANGLE_STRIP, DEMO_TRIANGLE_FAN, DEMO_QUADS
};

enum demo_blend {
	DEMO_BLEND_NONE,	/* Source replaces the frame */
	DEMO_BLEND_ALPHA,	/* src * a + dst * (1 - a) */
	DEMO_BLEND_ADD,		/* src * a + dst */
	DEMO_BLEND_MULTIPLY,	/* dst * src * 2 (GL_DST_COLOR, GL_SRC_COLOR) */
};

enum demo_fog { DEMO_FOG_NONE, DEMO_FOG_LINEAR, DEMO_FOG_EXP, DEMO_FOG_EXP2 };

/* Textures: RGBA bytes, width and height powers of two */
#define DEMO_TEX_CHECKER	1
#define DEMO_TEX_GRADIENT	2
#define DEMO_TEX_ALPHA		3
#define DEMO_TEX_LIGHT		4
#define DEMO_TEX_BRICK		5
#define DEMO_TEX_SIZES		6	/* 8 textures: 64, 128, 256, 512x256 at 16 bits, then at 32 bits */
#define DEMO_TEX_MANY		14	/* 24 textures of 256x256: more than the GS memory holds */
#define DEMO_TEX_MANY_COUNT	24
#define DEMO_TEX_LIGHTMAP	38	/* 128x128, part of it changes each frame */
#define DEMO_TEX_COUNT		38

/* --- the back end ---------------------------------------------------------- */

void d_texture_create(int id, int width, int height, const uint8_t *rgba);
void d_texture_update(int id, int x, int y, int width, int height, const uint8_t *rgba);
void d_clear(float r, float g, float b);
void d_frustum(float l, float r, float b, float t, float n, float f);
void d_ortho(float l, float r, float b, float t, float n, float f);
void d_modelview(const float m[16]);	/* Column major, as OpenGL */
void d_depth(int test, int write);	/* Test: less or equal */
void d_blend(enum demo_blend mode);
void d_alpha_test(int on, float ref);	/* Pass when alpha > ref */
void d_fog(enum demo_fog mode, float density, float start, float end,
	const float color[3]);
void d_color_mask(int on);
void d_texture(int id, int linear, int clamp);	/* id 0: no texture */
void d_smooth(int on);
void d_begin(enum demo_prim prim);
void d_color(float r, float g, float b, float a);
void d_texcoord(float s, float t);
void d_vertex(float x, float y, float z);
void d_end(void);

/* --- the scenes -------------------------------------------------------------- */

int demo_scene_count(void);
const char *demo_scene_name(int scene);
void demo_make_textures(void);
void demo_scene_draw(int scene, int frame);

/* --- shared math ---------------------------------------------------------- */

void m_identity(float m[16]);
void m_multiply(float out[16], const float a[16], const float b[16]);
void m_translate(float m[16], float x, float y, float z);
void m_rotate_y(float m[16], float degrees);
void m_rotate_x(float m[16], float degrees);
void m_frustum(float m[16], float l, float r, float b, float t, float n, float f);
void m_ortho(float m[16], float l, float r, float b, float t, float n, float f);

#endif
