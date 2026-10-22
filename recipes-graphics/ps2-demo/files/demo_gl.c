// SPDX-License-Identifier: MIT
/* The OpenGL back end of the demo: libGL.so.1 on VU1 and the GS. */

#include <stdio.h>
#include <stdlib.h>

#include <GL/gl.h>

#include "demo.h"
#include "demo_main.h"

static GLenum prim_of(enum demo_prim p)
{
	switch (p) {
	case DEMO_POINTS:		return GL_POINTS;
	case DEMO_LINES:		return GL_LINES;
	case DEMO_LINE_STRIP:		return GL_LINE_STRIP;
	case DEMO_TRIANGLES:		return GL_TRIANGLES;
	case DEMO_TRIANGLE_STRIP:	return GL_TRIANGLE_STRIP;
	case DEMO_TRIANGLE_FAN:		return GL_TRIANGLE_FAN;
	default:			return GL_QUADS;
	}
}

void d_texture_create(int id, int width, int height, const uint8_t *rgba)
{
	glBindTexture(GL_TEXTURE_2D, id);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, rgba);
}

void d_texture_update(int id, int x, int y, int width, int height, const uint8_t *rgba)
{
	glBindTexture(GL_TEXTURE_2D, id);
	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA,
		GL_UNSIGNED_BYTE, rgba);
}

void d_clear(float r, float g, float b)
{
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(r, g, b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void d_frustum(float l, float r, float b, float t, float n, float f)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(l, r, b, t, n, f);
	glMatrixMode(GL_MODELVIEW);
}

void d_ortho(float l, float r, float b, float t, float n, float f)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(l, r, b, t, n, f);
	glMatrixMode(GL_MODELVIEW);
}

void d_modelview(const float m[16])
{
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(m);
}

void d_depth(int test, int write)
{
	if (test)
		glEnable(GL_DEPTH_TEST);
	else
		glDisable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(write ? GL_TRUE : GL_FALSE);
}

void d_blend(enum demo_blend mode)
{
	if (mode == DEMO_BLEND_NONE) {
		glDisable(GL_BLEND);
		return;
	}
	glEnable(GL_BLEND);
	switch (mode) {
	case DEMO_BLEND_ALPHA:		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
	case DEMO_BLEND_ADD:		glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
	default:			glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR); break;
	}
}

void d_alpha_test(int on, float ref)
{
	if (on) {
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, ref);
	} else {
		glDisable(GL_ALPHA_TEST);
	}
}

void d_fog(enum demo_fog mode, float density, float start, float end,
	const float color[3])
{
	GLfloat c[4] = { 0, 0, 0, 1 };

	if (mode == DEMO_FOG_NONE) {
		glDisable(GL_FOG);
		return;
	}
	c[0] = color[0];
	c[1] = color[1];
	c[2] = color[2];
	glEnable(GL_FOG);
	glFogi(GL_FOG_MODE, mode == DEMO_FOG_LINEAR ? GL_LINEAR :
		mode == DEMO_FOG_EXP ? GL_EXP : GL_EXP2);
	glFogf(GL_FOG_DENSITY, density);
	glFogf(GL_FOG_START, start);
	glFogf(GL_FOG_END, end);
	glFogfv(GL_FOG_COLOR, c);
}

void d_color_mask(int on)
{
	glColorMask(on, on, on, on);
}

void d_texture(int id, int linear, int clamp)
{
	if (!id) {
		glDisable(GL_TEXTURE_2D);
		return;
	}
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp ? GL_CLAMP : GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp ? GL_CLAMP : GL_REPEAT);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

void d_smooth(int on)
{
	glShadeModel(on ? GL_SMOOTH : GL_FLAT);
}

void d_begin(enum demo_prim prim)
{
	glBegin(prim_of(prim));
}

void d_color(float r, float g, float b, float a)
{
	glColor4f(r, g, b, a);
}

void d_texcoord(float s, float t)
{
	glTexCoord2f(s, t);
}

void d_vertex(float x, float y, float z)
{
	glVertex3f(x, y, z);
}

void d_end(void)
{
	glEnd();
}

int demo_open(void)
{
	if (!demo_vsync)
		setenv("PS2GL_VSYNC", "0", 1);
	if (ps2glInit() < 0)
		return -1;
	glViewport(0, 0, ps2glWidth(), ps2glHeight());
	return 0;
}

void demo_swap(void)
{
	ps2glSwapBuffers();
}

int demo_key(int *code, int *pressed)
{
	return ps2glGetKey(code, pressed);
}

void demo_close(void)
{
	ps2glExit();
}

int main(int argc, char **argv)
{
	return demo_main(argc, argv, "ps2demo-gl");
}
