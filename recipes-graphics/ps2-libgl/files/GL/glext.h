/* SPDX-License-Identifier: MIT */
/*
 * OpenGL extension names for the PlayStation 2 OpenGL library. Programs
 * include this header for the names of extensions that they test at run
 * time with glGetString(GL_EXTENSIONS) or SDL_GL_GetProcAddress. The
 * library has none of them. The names let the programs compile.
 */
#ifndef __glext_h_
#define __glext_h_

#include <GL/gl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#define GL_GLEXT_VERSION		20260101

typedef int64_t GLint64;
typedef uint64_t GLuint64;
typedef intptr_t GLintptr;
typedef intptr_t GLsizeiptr;
typedef char GLchar;
typedef struct __GLsync *GLsync;

/* GL_ARB_multitexture */
#define GL_ARB_multitexture		1
#define GL_TEXTURE0_ARB			0x84C0
#define GL_TEXTURE1_ARB			0x84C1
#define GL_TEXTURE2_ARB			0x84C2
#define GL_TEXTURE3_ARB			0x84C3
#define GL_ACTIVE_TEXTURE_ARB		0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE_ARB	0x84E1
#define GL_MAX_TEXTURE_UNITS_ARB	0x84E2
typedef void (APIENTRYP PFNGLACTIVETEXTUREARBPROC)(GLenum texture);
typedef void (APIENTRYP PFNGLCLIENTACTIVETEXTUREARBPROC)(GLenum texture);
typedef void (APIENTRYP PFNGLMULTITEXCOORD2FARBPROC)(GLenum target, GLfloat s, GLfloat t);
typedef void (APIENTRYP PFNGLMULTITEXCOORD2FVARBPROC)(GLenum target, const GLfloat *v);

/* GL_SGIS_multitexture (the old form) */
#define GL_SGIS_multitexture		1
#define GL_TEXTURE0_SGIS		0x835E
#define GL_TEXTURE1_SGIS		0x835F
#define GL_MAX_TEXTURES_SGIS		0x835D
typedef void (APIENTRYP PFNGLSELECTTEXTURESGISPROC)(GLenum target);
typedef void (APIENTRYP PFNGLMULTITEXCOORD2FSGISPROC)(GLenum target, GLfloat s, GLfloat t);

/* GL_EXT_texture_filter_anisotropic */
#define GL_EXT_texture_filter_anisotropic 1
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT	0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

/* GL_EXT_texture (internal formats with a size) */
#define GL_EXT_texture			1
#define GL_INTENSITY4_EXT		0x804A
#define GL_INTENSITY8_EXT		0x804B
#define GL_LUMINANCE4_EXT		0x803F
#define GL_LUMINANCE8_EXT		0x8040
#define GL_RGB5_EXT			0x8050
#define GL_RGB8_EXT			0x8051
#define GL_RGBA4_EXT			0x8056
#define GL_RGB5_A1_EXT			0x8057
#define GL_RGBA8_EXT			0x8058

/* GL_EXT_paletted_texture: names only, the library does not take it */
#define GL_COLOR_INDEX8_EXT		0x80E5
#define GL_TEXTURE_INDEX_SIZE_EXT	0x80ED

/* GL_SGIS_generate_mipmap */
#define GL_SGIS_generate_mipmap		1
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP		0x8191
#define GL_GENERATE_MIPMAP_HINT		0x8192
#endif
#define GL_GENERATE_MIPMAP_SGIS		0x8191

/* GL_ARB_sync */
#define GL_ARB_sync			1
#define GL_SYNC_FLUSH_COMMANDS_BIT	0x00000001
#define GL_SYNC_GPU_COMMANDS_COMPLETE	0x9117
#define GL_ALREADY_SIGNALED		0x911A
#define GL_TIMEOUT_EXPIRED		0x911B
#define GL_CONDITION_SATISFIED		0x911C
#define GL_WAIT_FAILED			0x911D
#define GL_TIMEOUT_IGNORED		0xFFFFFFFFFFFFFFFFull
typedef GLsync (APIENTRYP PFNGLFENCESYNCPROC)(GLenum condition, GLbitfield flags);
typedef GLboolean (APIENTRYP PFNGLISSYNCPROC)(GLsync sync);
typedef void (APIENTRYP PFNGLDELETESYNCPROC)(GLsync sync);
typedef GLenum (APIENTRYP PFNGLCLIENTWAITSYNCPROC)(GLsync sync, GLbitfield flags, GLuint64 timeout);
typedef void (APIENTRYP PFNGLWAITSYNCPROC)(GLsync sync, GLbitfield flags, GLuint64 timeout);

/* GL_ARB_vertex_buffer_object */
#define GL_ARRAY_BUFFER_ARB		0x8892
#define GL_ELEMENT_ARRAY_BUFFER_ARB	0x8893
#define GL_STATIC_DRAW_ARB		0x88E4
#define GL_DYNAMIC_DRAW_ARB		0x88E8
typedef void (APIENTRYP PFNGLBINDBUFFERARBPROC)(GLenum target, GLuint buffer);
typedef void (APIENTRYP PFNGLDELETEBUFFERSARBPROC)(GLsizei n, const GLuint *buffers);
typedef void (APIENTRYP PFNGLGENBUFFERSARBPROC)(GLsizei n, GLuint *buffers);
typedef void (APIENTRYP PFNGLBUFFERDATAARBPROC)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void (APIENTRYP PFNGLBUFFERSUBDATAARBPROC)(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);

/* GL_NV_register_combiners: names only */
#define GL_REGISTER_COMBINERS_NV	0x8522
#define GL_VARIABLE_A_NV		0x8523
#define GL_VARIABLE_B_NV		0x8524
#define GL_VARIABLE_C_NV		0x8525
#define GL_VARIABLE_D_NV		0x8526
#define GL_VARIABLE_E_NV		0x8527
#define GL_VARIABLE_F_NV		0x8528
#define GL_VARIABLE_G_NV		0x8529
#define GL_CONSTANT_COLOR0_NV		0x852A
#define GL_CONSTANT_COLOR1_NV		0x852B
#define GL_PRIMARY_COLOR_NV		0x852C
#define GL_SECONDARY_COLOR_NV		0x852D
#define GL_SPARE0_NV			0x852E
#define GL_SPARE1_NV			0x852F
#define GL_DISCARD_NV			0x8530
#define GL_E_TIMES_F_NV			0x8531
#define GL_SPARE0_PLUS_SECONDARY_COLOR_NV 0x8532
#define GL_UNSIGNED_IDENTITY_NV		0x8536
#define GL_UNSIGNED_INVERT_NV		0x8537
#define GL_EXPAND_NORMAL_NV		0x8538
#define GL_EXPAND_NEGATE_NV		0x8539
#define GL_HALF_BIAS_NORMAL_NV		0x853A
#define GL_HALF_BIAS_NEGATE_NV		0x853B
#define GL_SIGNED_IDENTITY_NV		0x853C
#define GL_SIGNED_NEGATE_NV		0x853D
#define GL_SCALE_BY_TWO_NV		0x853E
#define GL_SCALE_BY_FOUR_NV		0x853F
#define GL_SCALE_BY_ONE_HALF_NV		0x8540
#define GL_BIAS_BY_NEGATIVE_ONE_HALF_NV	0x8541
#define GL_COMBINER_INPUT_NV		0x8542
#define GL_COMBINER_MAPPING_NV		0x8543
#define GL_COMBINER_COMPONENT_USAGE_NV	0x8544
#define GL_COMBINER_AB_DOT_PRODUCT_NV	0x8545
#define GL_COMBINER_CD_DOT_PRODUCT_NV	0x8546
#define GL_COMBINER_MUX_SUM_NV		0x8547
#define GL_COMBINER_SCALE_NV		0x8548
#define GL_COMBINER_BIAS_NV		0x8549
#define GL_COMBINER_AB_OUTPUT_NV	0x854A
#define GL_COMBINER_CD_OUTPUT_NV	0x854B
#define GL_COMBINER_SUM_OUTPUT_NV	0x854C
#define GL_MAX_GENERAL_COMBINERS_NV	0x854D
#define GL_NUM_GENERAL_COMBINERS_NV	0x854E
#define GL_COLOR_SUM_CLAMP_NV		0x854F
#define GL_COMBINER0_NV			0x8550
#define GL_COMBINER1_NV			0x8551
#define GL_COMBINER2_NV			0x8552
#define GL_COMBINER3_NV			0x8553
#define GL_COMBINER4_NV			0x8554
#define GL_COMBINER5_NV			0x8555
#define GL_COMBINER6_NV			0x8556
#define GL_COMBINER7_NV			0x8557

#ifdef __cplusplus
}
#endif

#endif
