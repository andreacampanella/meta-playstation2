// SPDX-License-Identifier: MIT
/*
 * The main loop of both demos. The back end gives the display, the frame
 * swap and the keys through these three functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "demo.h"
#include "demo_main.h"

#define KEY_ESC		1
#define KEY_ENTER	28
#define KEY_SPACE	57
#define KEY_LEFT	105
#define KEY_RIGHT	106

int demo_vsync = 1;

int demo_main(int argc, char **argv, const char *name)
{
	int scene = 0, frame = 0, frames = 0, limit = 0, code, pressed, i;
	struct timeval start, now;
	int hold = 0;		/* Frames each scene stays in the automatic run */

	if (getenv("PS2GL_VSYNC") && atoi(getenv("PS2GL_VSYNC")) == 0)
		demo_vsync = 0;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--novsync"))
			demo_vsync = 0;
		else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
			limit = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--hold") && i + 1 < argc)
			hold = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--help")) {
			printf("usage: %s [scene] [--frames n] [--hold n] [--novsync]\n"
				"Space or Enter: next scene. Left: the scene before. Escape: quit.\n"
				"--frames n: stop after n frames and print the frame rate.\n"
				"--hold n: go to the next scene every n frames.\n"
				"--novsync: no wait for the vertical sync (for a frame rate above 50).\nScenes:\n", name);
			for (i = 0; i < demo_scene_count(); i++)
				printf("  %d %s\n", i, demo_scene_name(i));
			return 0;
		} else {
			scene = atoi(argv[i]);
			if (scene < 0 || scene >= demo_scene_count())
				scene = 0;
		}
	}

	if (demo_open() < 0)
		return 1;
	demo_make_textures();
	gettimeofday(&start, NULL);
	for (;;) {
		int quit = 0;

		demo_scene_draw(scene, frame);
		demo_swap();
		frame++;
		frames++;

		while (demo_key(&code, &pressed)) {
			if (!pressed)
				continue;
			if (code == KEY_ESC)
				quit = 1;
			else if (code == KEY_SPACE || code == KEY_ENTER || code == KEY_RIGHT) {
				scene = (scene + 1) % demo_scene_count();
				frame = 0;
			} else if (code == KEY_LEFT) {
				scene = (scene + demo_scene_count() - 1) % demo_scene_count();
				frame = 0;
			}
		}
		if (hold && frame >= hold) {
			scene = (scene + 1) % demo_scene_count();
			frame = 0;
		}
		if (quit || (limit && frames >= limit))
			break;
	}
	gettimeofday(&now, NULL);
	{
		const double seconds = (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1e6;

		if (seconds > 0.0)
			printf("%s: %d frames in %.1f s: %.1f frames/s\n", name, frames,
				seconds, frames / seconds);
	}
	demo_close();
	return 0;
}
