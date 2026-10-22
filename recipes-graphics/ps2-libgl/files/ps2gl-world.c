// SPDX-License-Identifier: MIT
/*
 * ps2gl-world: a room with textures that you can walk in, drawn with the
 * ps2-libgl library (libGL.so.1). Vector unit 1 does the vertex math and the Graphics
 * Synthesizer draws the textures.
 *
 * usage: ps2gl-world [frames]
 *
 * Keys: arrows to move and turn, Escape to stop. With no key for 5 seconds,
 * the camera moves on its own path. With a frame count, the program stops
 * after that number of frames and prints the frame rate.
 *
 * The model is in vertex arrays, so the CPU does no floating point
 * operation for a vertex.
 *
 * The library does not cut a triangle at the camera. It rejects a triangle
 * if one corner is behind the camera. So a surface near the camera can get
 * a hole. Two measures prevent that here: the walls have squares of half a
 * unit, and the camera stays MARGIN units away from walls and boxes.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <GL/gl.h>

/* Linux key codes */
#define KEY_ESC		1
#define KEY_UP		103
#define KEY_LEFT	105
#define KEY_RIGHT	106
#define KEY_DOWN	108

#define ROOM	8	/* The floor goes from -ROOM to +ROOM */
#define HEIGHT	3
#define TEX	64
#define MARGIN	1.0f	/* Smallest distance from the camera to a surface */

/* The boxes, for the model and for the collision test */
static const struct box {
	GLfloat x, z, size, height;
} boxes[] = {
	{ -4.0f, -4.0f, 1.0f, 1.0f },
	{ 4.5f, -3.0f, 1.0f, 1.0f },
	{ 4.5f, -1.9f, 1.0f, 1.0f },
	{ 4.5f, -2.5f, 1.0f, 2.0f },
	{ -3.0f, 4.0f, 1.5f, 1.5f },
	{ 2.0f, 5.0f, 1.0f, 1.0f },
	{ 0.0f, 0.0f, 0.8f, 2.6f },
};
#define BOXES ((int)(sizeof(boxes) / sizeof(boxes[0])))

struct vertex {
	GLfloat x, y, z;
	GLfloat s, t;
	GLubyte r, g, b, a;
};

static struct vertex *model;
static int model_count, model_capacity;

/* Vertex ranges in the model, one for each texture */
static int floor_first, floor_count;
static int wall_first, wall_count;
static int crate_first, crate_count;

static long long now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void add_vertex(GLfloat x, GLfloat y, GLfloat z, GLfloat s, GLfloat t,
	int shade)
{
	struct vertex *v;

	if (model_count == model_capacity) {
		model_capacity = model_capacity ? 2 * model_capacity : 4096;
		model = realloc(model, model_capacity * sizeof(*model));
		if (!model)
			exit(1);
	}
	v = &model[model_count++];
	v->x = x;
	v->y = y;
	v->z = z;
	v->s = s;
	v->t = t;
	v->r = v->g = v->b = shade;
	v->a = 255;
}

/*
 * A flat rectangle of n x m squares, as triangles. origin is one corner, and
 * u and v are the two edges of one square. The texture repeats after
 * 1 / tscale squares.
 */
static void add_grid(const GLfloat *origin, const GLfloat *u, const GLfloat *v,
	int n, int m, GLfloat tscale, int shade)
{
	int i, j, k;

	for (i = 0; i < n; i++)
		for (j = 0; j < m; j++) {
			static const int corner[6][2] = {
				{ 0, 0 }, { 1, 0 }, { 1, 1 },
				{ 0, 0 }, { 1, 1 }, { 0, 1 },
			};

			for (k = 0; k < 6; k++) {
				const int a = i + corner[k][0], b = j + corner[k][1];

				add_vertex(origin[0] + a * u[0] + b * v[0],
					   origin[1] + a * u[1] + b * v[1],
					   origin[2] + a * u[2] + b * v[2],
					   a * tscale, b * tscale, shade);
			}
		}
}

static void add_box(GLfloat cx, GLfloat cz, GLfloat size, GLfloat height)
{
	const GLfloat h = size * 0.5f;
	const GLfloat x0 = cx - h, x1 = cx + h, z0 = cz - h, z1 = cz + h;
	const GLfloat a[3] = { x0, 0, z1 }, ax[3] = { size, 0, 0 };
	const GLfloat b[3] = { x1, 0, z1 }, bz[3] = { 0, 0, -size };
	const GLfloat c[3] = { x1, 0, z0 }, cx_[3] = { -size, 0, 0 };
	const GLfloat d[3] = { x0, 0, z0 }, dz[3] = { 0, 0, size };
	const GLfloat top[3] = { x0, height, z1 };
	const GLfloat up[3] = { 0, height, 0 };

	add_grid(a, ax, up, 1, 1, 1.0f, 255);
	add_grid(b, bz, up, 1, 1, 1.0f, 200);
	add_grid(c, cx_, up, 1, 1, 1.0f, 150);
	add_grid(d, dz, up, 1, 1, 1.0f, 200);
	add_grid(top, ax, bz, 1, 1, 1.0f, 255);
}

static void build_model(void)
{
	static const GLfloat x1[3] = { 1, 0, 0 }, z1[3] = { 0, 0, 1 };
	/* Wall squares have half the size of floor squares. */
	static const GLfloat xh[3] = { 0.5f, 0, 0 }, zh[3] = { 0, 0, 0.5f };
	static const GLfloat xm[3] = { -0.5f, 0, 0 }, zm[3] = { 0, 0, -0.5f };
	static const GLfloat yh[3] = { 0, 0.5f, 0 };
	const GLfloat floor_origin[3] = { -ROOM, 0, -ROOM };
	const GLfloat ceiling_origin[3] = { -ROOM, HEIGHT, -ROOM };
	const GLfloat north[3] = { -ROOM, 0, -ROOM };
	const GLfloat east[3] = { ROOM, 0, -ROOM };
	const GLfloat south[3] = { ROOM, 0, ROOM };
	const GLfloat west[3] = { -ROOM, 0, ROOM };
	int i;

	floor_first = model_count;
	add_grid(floor_origin, x1, z1, 2 * ROOM, 2 * ROOM, 1.0f, 255);
	add_grid(ceiling_origin, x1, z1, 2 * ROOM, 2 * ROOM, 1.0f, 110);
	floor_count = model_count - floor_first;

	wall_first = model_count;
	add_grid(north, xh, yh, 4 * ROOM, 2 * HEIGHT, 0.5f, 255);
	add_grid(east, zh, yh, 4 * ROOM, 2 * HEIGHT, 0.5f, 190);
	add_grid(south, xm, yh, 4 * ROOM, 2 * HEIGHT, 0.5f, 255);
	add_grid(west, zm, yh, 4 * ROOM, 2 * HEIGHT, 0.5f, 190);
	wall_count = model_count - wall_first;

	crate_first = model_count;
	for (i = 0; i < BOXES; i++)
		add_box(boxes[i].x, boxes[i].z, boxes[i].size, boxes[i].height);
	crate_count = model_count - crate_first;
}

/* Keep the camera MARGIN units away from the walls and the boxes. */
static void collide(GLfloat *x, GLfloat *z)
{
	const GLfloat limit = ROOM - MARGIN;
	int i;

	for (i = 0; i < BOXES; i++) {
		const GLfloat half = boxes[i].size * 0.5f + MARGIN;
		const GLfloat dx = *x - boxes[i].x, dz = *z - boxes[i].z;
		const GLfloat ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz;

		if (ax >= half || az >= half)
			continue;
		/* Inside: move out through the nearest side. */
		if (half - ax < half - az)
			*x = boxes[i].x + (dx < 0 ? -half : half);
		else
			*z = boxes[i].z + (dz < 0 ? -half : half);
	}

	if (*x > limit) *x = limit;
	if (*x < -limit) *x = -limit;
	if (*z > limit) *z = limit;
	if (*z < -limit) *z = -limit;
}

/* The textures are made here, so the program needs no picture files. */
static void make_texture(GLuint id, int kind)
{
	static GLubyte pixels[TEX * TEX * 4];
	uint32_t seed = 1234 + kind;
	int x, y;

	for (y = 0; y < TEX; y++)
		for (x = 0; x < TEX; x++) {
			GLubyte *p = &pixels[4 * (y * TEX + x)];
			int noise, r, g, b;

			seed = seed * 1664525u + 1013904223u;
			noise = (seed >> 24) & 31;

			if (kind == 0) {
				/* Floor: two colors of tiles with dark joints */
				const int tile = ((x / 32) + (y / 32)) & 1;
				const int joint = (x % 32) < 2 || (y % 32) < 2;

				r = g = b = joint ? 40 : (tile ? 150 : 95) + noise;
				b += joint ? 0 : 15;
			} else if (kind == 1) {
				/* Wall: bricks, each second row moved by half a brick */
				const int row = y / 16;
				const int bx = (x + (row & 1) * 16) % 32;
				const int joint = (y % 16) < 2 || bx < 2;

				r = joint ? 150 : 150 + noise;
				g = joint ? 145 : 60 + noise / 2;
				b = joint ? 135 : 45 + noise / 2;
			} else {
				/* Crate: planks with a dark frame and a cross */
				const int frame = x < 5 || y < 5 || x >= TEX - 5 || y >= TEX - 5;
				const int cross = abs(x - y) < 3 || abs(x + y - (TEX - 1)) < 3;
				const int plank = (x % 16) < 1;

				r = (frame || cross ? 110 : 175) + noise - (plank ? 40 : 0);
				g = (frame || cross ? 75 : 130) + noise - (plank ? 30 : 0);
				b = (frame || cross ? 35 : 70) + noise / 2 - (plank ? 20 : 0);
			}

			p[0] = r < 0 ? 0 : r > 255 ? 255 : r;
			p[1] = g < 0 ? 0 : g > 255 ? 255 : g;
			p[2] = b < 0 ? 0 : b > 255 ? 255 : b;
			p[3] = 255;
		}

	glBindTexture(GL_TEXTURE_2D, id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX, TEX, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, pixels);
}

int main(int argc, char *argv[])
{
	const int max_frames = argc > 1 ? atoi(argv[1]) : 0;
	GLfloat cam_x = 0.0f, cam_z = 5.5f, angle = 0.0f;	/* Degrees */
	int key_up = 0, key_down = 0, key_left = 0, key_right = 0;
	long long t0, last, last_key;
	GLuint texture[3];
	int frames = 0, done = 0;

	build_model();

	if (ps2glInit() != 0)
		return 1;

	glGenTextures(3, texture);
	make_texture(texture[0], 0);
	make_texture(texture[1], 1);
	make_texture(texture[2], 2);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	ps2glPerspective(60.0f, 4.0f / 3.0f, 0.2f, 60.0f);
	glMatrixMode(GL_MODELVIEW);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(3, GL_FLOAT, sizeof(struct vertex), &model[0].x);
	glTexCoordPointer(2, GL_FLOAT, sizeof(struct vertex), &model[0].s);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(struct vertex), &model[0].r);

	t0 = last = now_us();
	last_key = t0 - 10000000;

	while (!done) {
		const long long now = now_us();
		const GLfloat dt = (now - last) * 1e-6f;
		int code, pressed;

		last = now;

		while (ps2glGetKey(&code, &pressed)) {
			last_key = now;
			if (code == KEY_ESC && pressed)
				done = 1;
			else if (code == KEY_UP)
				key_up = pressed;
			else if (code == KEY_DOWN)
				key_down = pressed;
			else if (code == KEY_LEFT)
				key_left = pressed;
			else if (code == KEY_RIGHT)
				key_right = pressed;
		}

		if (now - last_key > 5000000) {
			/* No key: walk on a circle and look along the path. */
			const GLfloat a = (now - t0) * 0.25e-6f;

			cam_x = 5.0f * sinf(a);
			cam_z = 5.0f * cosf(a);
			collide(&cam_x, &cam_z);
			angle = -a * (180.0f / 3.14159265f) - 90.0f + 35.0f * sinf(a * 2.0f);
		} else {
			const GLfloat rad = angle * (3.14159265f / 180.0f);
			const GLfloat step = (key_up - key_down) * 3.0f * dt;

			angle += (key_left - key_right) * 90.0f * dt;
			cam_x -= sinf(rad) * step;
			cam_z -= cosf(rad) * step;
			collide(&cam_x, &cam_z);
		}

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glLoadIdentity();
		glRotatef(-angle, 0.0f, 1.0f, 0.0f);
		glTranslatef(-cam_x, -1.5f, -cam_z);

		glBindTexture(GL_TEXTURE_2D, texture[0]);
		glDrawArrays(GL_TRIANGLES, floor_first, floor_count);
		glBindTexture(GL_TEXTURE_2D, texture[1]);
		glDrawArrays(GL_TRIANGLES, wall_first, wall_count);
		glBindTexture(GL_TEXTURE_2D, texture[2]);
		glDrawArrays(GL_TRIANGLES, crate_first, crate_count);

		ps2glSwapBuffers();

		frames++;
		if (max_frames && frames >= max_frames)
			done = 1;
	}

	ps2glExit();
	printf("%d frames in %lld ms: %lld frames each second, %d triangles\n",
		frames, (now_us() - t0) / 1000,
		now_us() > t0 ? (long long)frames * 1000000 / (now_us() - t0) : 0,
		model_count / 3);

	return 0;
}
