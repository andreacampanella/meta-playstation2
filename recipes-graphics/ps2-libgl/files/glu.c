// SPDX-License-Identifier: MIT
/*
 * libGLU.so.1 for the PlayStation 2 OpenGL library. Games such as
 * DXX-Rebirth (Descent) ask for GLU for the perspective matrix and for
 * gluBuild2DMipmaps. The library draws one texture level, so the mipmap
 * builder loads level 0 only.
 */
#include <math.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/glu.h>

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar)
{
	const GLdouble ymax = zNear * tan(fovy * M_PI / 360.0);
	const GLdouble xmax = ymax * aspect;

	glFrustum(-xmax, xmax, -ymax, ymax, zNear, zFar);
}

void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top)
{
	glOrtho(left, right, bottom, top, -1.0, 1.0);
}

static void normalize(GLfloat v[3])
{
	const GLfloat r = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

	if (r > 0.0f) {
		v[0] /= r;
		v[1] /= r;
		v[2] /= r;
	}
}

static void cross(GLfloat out[3], const GLfloat a[3], const GLfloat b[3])
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

void gluLookAt(GLdouble eyeX, GLdouble eyeY, GLdouble eyeZ,
	GLdouble centerX, GLdouble centerY, GLdouble centerZ,
	GLdouble upX, GLdouble upY, GLdouble upZ)
{
	GLfloat f[3] = { (GLfloat)(centerX - eyeX), (GLfloat)(centerY - eyeY), (GLfloat)(centerZ - eyeZ) };
	GLfloat up[3] = { (GLfloat)upX, (GLfloat)upY, (GLfloat)upZ };
	GLfloat s[3], u[3], m[16];

	normalize(f);
	cross(s, f, up);
	normalize(s);
	cross(u, s, f);
	memset(m, 0, sizeof(m));
	m[0] = s[0];  m[4] = s[1];  m[8] = s[2];
	m[1] = u[0];  m[5] = u[1];  m[9] = u[2];
	m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2];
	m[15] = 1.0f;
	glMultMatrixf(m);
	glTranslatef((GLfloat)-eyeX, (GLfloat)-eyeY, (GLfloat)-eyeZ);
}

GLint gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width,
	GLsizei height, GLenum format, GLenum type, const void *data)
{
	glTexImage2D(target, 0, internalFormat, width, height, 0, format, type, data);
	return 0;
}

GLint gluScaleImage(GLenum format, GLsizei wIn, GLsizei hIn, GLenum typeIn,
	const void *dataIn, GLsizei wOut, GLsizei hOut, GLenum typeOut, GLvoid *dataOut)
{
	(void)format;
	(void)wIn;
	(void)hIn;
	(void)typeIn;
	(void)dataIn;
	(void)wOut;
	(void)hOut;
	(void)typeOut;
	(void)dataOut;
	return GLU_INVALID_OPERATION;
}

const GLubyte *gluErrorString(GLenum error)
{
	switch (error) {
	case GL_NO_ERROR:		return (const GLubyte *)"no error";
	case GL_INVALID_ENUM:
	case GLU_INVALID_ENUM:		return (const GLubyte *)"invalid enumerant";
	case GL_INVALID_VALUE:
	case GLU_INVALID_VALUE:		return (const GLubyte *)"invalid value";
	case GL_INVALID_OPERATION:
	case GLU_INVALID_OPERATION:	return (const GLubyte *)"invalid operation";
	case GL_OUT_OF_MEMORY:
	case GLU_OUT_OF_MEMORY:		return (const GLubyte *)"out of memory";
	default:			return (const GLubyte *)"unknown error";
	}
}

const GLubyte *gluGetString(GLenum name)
{
	switch (name) {
	case GLU_VERSION:	return (const GLubyte *)"1.2 ps2gl";
	default:		return (const GLubyte *)"";
	}
}
