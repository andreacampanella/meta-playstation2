// SPDX-License-Identifier: MIT
#ifndef DEMO_MAIN_H
#define DEMO_MAIN_H

/* The back end: open the display, show a frame, read a key, close */
int demo_open(void);
void demo_swap(void);
int demo_key(int *code, int *pressed);
void demo_close(void);

int demo_main(int argc, char **argv, const char *name);

/* 0 with --novsync or PS2GL_VSYNC=0: the frames go out as fast as they come */
extern int demo_vsync;

#endif
