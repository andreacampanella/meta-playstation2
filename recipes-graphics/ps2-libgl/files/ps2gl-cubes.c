// SPDX-License-Identifier: MIT
/*
 * ps2gl-cubes: 27 cubes that turn, drawn with the ps2-libgl library (libGL.so.1).
 *
 * usage: ps2gl-cubes [frames]
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <GL/gl.h>

static long long now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* Corner index bits: 1 = +x, 2 = +y, 4 = +z */
static void corner(int c)
{
	glColor3ub(c & 1 ? 255 : 40, c & 2 ? 255 : 40, c & 4 ? 255 : 40);
	glVertex3f(c & 1 ? 1.0f : -1.0f, c & 2 ? 1.0f : -1.0f, c & 4 ? 1.0f : -1.0f);
}

static void cube(void)
{
	static const unsigned char face[6][4] = {
		{ 1, 3, 7, 5 }, { 0, 2, 6, 4 }, { 2, 3, 7, 6 },
		{ 0, 1, 5, 4 }, { 4, 5, 7, 6 }, { 0, 1, 3, 2 },
	};
	int f, k;

	glBegin(GL_QUADS);
	for (f = 0; f < 6; f++)
		for (k = 0; k < 4; k++)
			corner(face[f][k]);
	glEnd();
}

int main(int argc, char *argv[])
{
	const int frames = argc > 1 ? atoi(argv[1]) : 600;
	long long t0, t1;
	int f, x, y, z;

	if (ps2glInit() != 0)
		return 1;

	printf("27 cubes, %d frames, %d triangles in each frame.\n"
	       "Do not type during the test.\n", frames, 27 * 12);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	/* The picture is 4:3 on the screen, also when the pixels are not square. */
	ps2glPerspective(50.0f, 4.0f / 3.0f, 1.0f, 100.0f);
	glMatrixMode(GL_MODELVIEW);
	glEnable(GL_DEPTH_TEST);
	glClearColor(0.0f, 0.0f, 0.2f, 1.0f);

	t0 = now_us();
	for (f = 0; f < frames; f++) {
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glLoadIdentity();
		glTranslatef(0.0f, 0.0f, -11.0f);
		glRotatef(f * 1.3f, 0.0f, 1.0f, 0.0f);
		glRotatef(f * 0.7f, 1.0f, 0.0f, 0.0f);

		for (x = -1; x <= 1; x++)
			for (y = -1; y <= 1; y++)
				for (z = -1; z <= 1; z++) {
					glPushMatrix();
					glTranslatef(x * 2.2f, y * 2.2f, z * 2.2f);
					glRotatef(f * 3.0f, 1.0f, 1.0f, 0.0f);
					glScalef(0.6f, 0.6f, 0.6f);
					cube();
					glPopMatrix();
				}

		ps2glSwapBuffers();
	}
	t1 = now_us();

	ps2glExit();
	printf("%d frames in %lld ms: %lld frames each second\n", frames,
		(t1 - t0) / 1000, t1 > t0 ? (long long)frames * 1000000 / (t1 - t0) : 0);

	return 0;
}
