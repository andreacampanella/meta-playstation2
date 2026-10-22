/* SPDX-License-Identifier: MIT */
/*
 * GLU for the PlayStation 2 OpenGL library: the matrix helpers, and the
 * mipmap builder as a plain texture load (the library has one level).
 */
#ifndef __glu_h__
#define __glu_h__

#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLAPI
#define GLAPI extern
#endif

#define GLU_VERSION_1_1		1
#define GLU_VERSION_1_2		1
#define GLU_VERSION		100800
#define GLU_EXTENSIONS		100801
#define GLU_INVALID_ENUM	100900
#define GLU_INVALID_VALUE	100901
#define GLU_OUT_OF_MEMORY	100902
#define GLU_INVALID_OPERATION	100904

GLAPI void APIENTRY gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);
GLAPI void APIENTRY gluLookAt(GLdouble eyeX, GLdouble eyeY, GLdouble eyeZ,
	GLdouble centerX, GLdouble centerY, GLdouble centerZ,
	GLdouble upX, GLdouble upY, GLdouble upZ);
GLAPI void APIENTRY gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top);
GLAPI GLint APIENTRY gluBuild2DMipmaps(GLenum target, GLint internalFormat,
	GLsizei width, GLsizei height, GLenum format, GLenum type, const void *data);
GLAPI GLint APIENTRY gluScaleImage(GLenum format, GLsizei wIn, GLsizei hIn, GLenum typeIn,
	const void *dataIn, GLsizei wOut, GLsizei hOut, GLenum typeOut, GLvoid *dataOut);
GLAPI const GLubyte *APIENTRY gluErrorString(GLenum error);
GLAPI const GLubyte *APIENTRY gluGetString(GLenum name);

#ifdef __cplusplus
}
#endif

#endif
