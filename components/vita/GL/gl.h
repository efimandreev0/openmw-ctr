/*
 * GL/gl.h wrapper for PS Vita (OpenMW port)
 *
 * Replaces VitaSDK's incomplete TinyGL gl.h with vitaGL's full desktop
 * GL-compatible API. We include vitaGL.h to get function declarations and
 * constants, then undo its #define-based types and replace with proper
 * typedefs that don't conflict with downstream code (especially OSG).
 */
#ifndef _OPENMW_VITA_GL_H_
#define _OPENMW_VITA_GL_H_

#include <stdint.h>
#include <stddef.h>

/* Prevent SDL2's SDL_opengl_glext.h from being included — its type
 * definitions (GLsizeiptr etc.) conflict with ours. We provide all
 * needed GL types and extensions via vitaGL. */
#define __glext_h_ 1
#define __gl_glext_h_ 1

/*
 * Include vitaGL.h for function declarations and GL constants.
 * vitaGL uses #define for GL types (e.g., #define GLint int32_t).
 * We'll undo these afterward to avoid conflicts with OSG typedefs.
 */
#include <vitaGL.h>

/*
 * Undo vitaGL's #define-based type names and replace with typedefs.
 * This prevents conflicts when other code does 'typedef ... GLsync' etc.
 *
 * Note: vitaGL's function declarations were already preprocessed with
 * the #define expansions (e.g., GLint→int32_t), so undoing the #defines
 * here doesn't affect them. The typedef'd types are ABI-compatible
 * (int==int32_t, unsigned int==uint32_t on ARM).
 */
#undef GLbitfield
#undef GLboolean
#undef GLbyte
#undef GLubyte
#undef GLchar
#undef GLshort
#undef GLushort
#undef GLint
#undef GLuint
#undef GLfixed
#undef GLint64
#undef GLuint64
#undef GLsizei
#undef GLenum
#undef GLintptr
#undef GLsizeiptr
#undef GLsync
#undef GLfloat
#undef GLclampf
#undef GLdouble
#undef GLclampd
#undef GLvoid
#undef GLclampx

/* Proper typedefs matching standard OpenGL headers */
typedef unsigned int    GLbitfield;
typedef unsigned char   GLboolean;
typedef signed char     GLbyte;
typedef unsigned char   GLubyte;
typedef char            GLchar;
typedef short           GLshort;
typedef unsigned short  GLushort;
typedef int             GLint;
typedef unsigned int    GLuint;
typedef int             GLfixed;
typedef int64_t         GLint64;
typedef uint64_t        GLuint64;
typedef int             GLsizei;
typedef unsigned int    GLenum;
typedef ptrdiff_t       GLintptr;
typedef ptrdiff_t       GLsizeiptr;
typedef struct __GLsync *GLsync;
typedef float           GLfloat;
typedef float           GLclampf;
typedef double          GLdouble;
typedef double          GLclampd;
typedef void            GLvoid;
typedef int             GLclampx;

/* Additional types for ARB extensions */
typedef ptrdiff_t       GLintptrARB;
typedef ptrdiff_t       GLsizeiptrARB;
typedef char            GLcharARB;
typedef unsigned int    GLhandleARB;
typedef unsigned short  GLhalf;
typedef unsigned short  GLhalfARB;

/* GL_APIENTRY for function pointer types */
#ifndef GL_APIENTRY
#define GL_APIENTRY
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef GLAPI
#define GLAPI extern
#endif

/* GL version macros */
#ifndef GL_VERSION_1_1
#define GL_VERSION_1_1 1
#endif

/* vitaGL incorrectly defines GL_DEPTH_COMPONENT32F as 0x8DAB (the NV value).
 * The correct ARB/core GL value is 0x8CAC. Fix it to avoid duplicate case
 * errors with GL_DEPTH_COMPONENT32F_NV in OSG. */
#ifdef GL_DEPTH_COMPONENT32F
#undef GL_DEPTH_COMPONENT32F
#endif
#define GL_DEPTH_COMPONENT32F 0x8CAC

/* Same issue: vitaGL defines GL_DEPTH32F_STENCIL8 as 0x8DAC (the NV value).
 * The correct ARB/core GL value is 0x8CAD. */
#ifdef GL_DEPTH32F_STENCIL8
#undef GL_DEPTH32F_STENCIL8
#endif
#define GL_DEPTH32F_STENCIL8 0x8CAD

/*
 * Missing GL constants not in vitaGL.h
 */

/* Buffers */
#ifndef GL_COLOR
#define GL_COLOR 0x1800
#endif
#ifndef GL_DEPTH
#define GL_DEPTH 0x1801
#endif
#ifndef GL_STENCIL_INDEX
#define GL_STENCIL_INDEX 0x1901
#endif
#ifndef GL_BITMAP
#define GL_BITMAP 0x1A00
#endif

/* Texture query */
#ifndef GL_TEXTURE_WIDTH
#define GL_TEXTURE_WIDTH 0x1000
#endif
#ifndef GL_TEXTURE_HEIGHT
#define GL_TEXTURE_HEIGHT 0x1001
#endif
#ifndef GL_TEXTURE_INTERNAL_FORMAT
#define GL_TEXTURE_INTERNAL_FORMAT 0x1003
#endif
#ifndef GL_TEXTURE_BINDING_1D
#define GL_TEXTURE_BINDING_1D 0x8068
#endif

/* Texture gen */
#ifndef GL_TEXTURE_GEN_S
#define GL_TEXTURE_GEN_S 0x0C60
#endif
#ifndef GL_TEXTURE_GEN_T
#define GL_TEXTURE_GEN_T 0x0C61
#endif
#ifndef GL_TEXTURE_GEN_R
#define GL_TEXTURE_GEN_R 0x0C62
#endif
#ifndef GL_TEXTURE_GEN_Q
#define GL_TEXTURE_GEN_Q 0x0C63
#endif
#ifndef GL_OBJECT_LINEAR
#define GL_OBJECT_LINEAR 0x2401
#endif
#ifndef GL_EYE_LINEAR
#define GL_EYE_LINEAR 0x2400
#endif
#ifndef GL_SPHERE_MAP
#define GL_SPHERE_MAP 0x2402
#endif

/* Pixel transfer */
#ifndef GL_PACK_ROW_LENGTH
#define GL_PACK_ROW_LENGTH 0x0D02
#endif

/* Sized internal formats */
#ifndef GL_R3_G3_B2
#define GL_R3_G3_B2 0x2A10
#endif
#ifndef GL_RGB4
#define GL_RGB4 0x804F
#endif
#ifndef GL_RGB5
#define GL_RGB5 0x8050
#endif
#ifndef GL_RGB10
#define GL_RGB10 0x8052
#endif
#ifndef GL_RGB12
#define GL_RGB12 0x8053
#endif
#ifndef GL_RGBA12
#define GL_RGBA12 0x805A
#endif
#ifndef GL_RGBA16
#define GL_RGBA16 0x805B
#endif
#ifndef GL_RGB10_A2
#define GL_RGB10_A2 0x8059
#endif

/* Luminance/intensity sized formats */
#ifndef GL_LUMINANCE4
#define GL_LUMINANCE4 0x803F
#endif
#ifndef GL_LUMINANCE8
#define GL_LUMINANCE8 0x8040
#endif
#ifndef GL_LUMINANCE12
#define GL_LUMINANCE12 0x8041
#endif
#ifndef GL_LUMINANCE16
#define GL_LUMINANCE16 0x8042
#endif
#ifndef GL_LUMINANCE4_ALPHA4
#define GL_LUMINANCE4_ALPHA4 0x8043
#endif
#ifndef GL_LUMINANCE6_ALPHA2
#define GL_LUMINANCE6_ALPHA2 0x8044
#endif
#ifndef GL_LUMINANCE8_ALPHA8
#define GL_LUMINANCE8_ALPHA8 0x8045
#endif
#ifndef GL_LUMINANCE12_ALPHA4
#define GL_LUMINANCE12_ALPHA4 0x8046
#endif
#ifndef GL_LUMINANCE12_ALPHA12
#define GL_LUMINANCE12_ALPHA12 0x8047
#endif
#ifndef GL_LUMINANCE16_ALPHA16
#define GL_LUMINANCE16_ALPHA16 0x8048
#endif
#ifndef GL_INTENSITY4
#define GL_INTENSITY4 0x804A
#endif
#ifndef GL_INTENSITY8
#define GL_INTENSITY8 0x804B
#endif
#ifndef GL_INTENSITY12
#define GL_INTENSITY12 0x804C
#endif
#ifndef GL_INTENSITY16
#define GL_INTENSITY16 0x804D
#endif

/* Pixel pack/unpack state */
#ifndef GL_PACK_SWAP_BYTES
#define GL_PACK_SWAP_BYTES 0x0D00
#endif
#ifndef GL_PACK_LSB_FIRST
#define GL_PACK_LSB_FIRST 0x0D01
#endif
#ifndef GL_PACK_SKIP_ROWS
#define GL_PACK_SKIP_ROWS 0x0D03
#endif
#ifndef GL_PACK_SKIP_PIXELS
#define GL_PACK_SKIP_PIXELS 0x0D04
#endif
#ifndef GL_UNPACK_SWAP_BYTES
#define GL_UNPACK_SWAP_BYTES 0x0CF0
#endif
#ifndef GL_UNPACK_LSB_FIRST
#define GL_UNPACK_LSB_FIRST 0x0CF1
#endif
#ifndef GL_UNPACK_SKIP_ROWS
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#endif
#ifndef GL_UNPACK_SKIP_PIXELS
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#endif

/* Missing sized formats */
#ifndef GL_RGB16
#define GL_RGB16 0x8054
#endif
#ifndef GL_ALPHA16
#define GL_ALPHA16 0x803E
#endif
#ifndef GL_RGBA4
#define GL_RGBA4 0x8056
#endif
#ifndef GL_RGB5_A1
#define GL_RGB5_A1 0x8057
#endif

/* Proxy textures */
#ifndef GL_PROXY_TEXTURE_1D
#define GL_PROXY_TEXTURE_1D 0x8063
#endif
#ifndef GL_PROXY_TEXTURE_2D
#define GL_PROXY_TEXTURE_2D 0x8064
#endif
#ifndef GL_PROXY_TEXTURE_CUBE_MAP
#define GL_PROXY_TEXTURE_CUBE_MAP 0x851B
#endif

/* Texture priority */
#ifndef GL_TEXTURE_PRIORITY
#define GL_TEXTURE_PRIORITY 0x8066
#endif

/* Logic ops */
#ifndef GL_COLOR_LOGIC_OP
#define GL_COLOR_LOGIC_OP 0x0BF2
#endif
#ifndef GL_AND
#define GL_AND 0x1501
#endif
#ifndef GL_AND_REVERSE
#define GL_AND_REVERSE 0x1502
#endif
#ifndef GL_AND_INVERTED
#define GL_AND_INVERTED 0x1504
#endif
#ifndef GL_NOOP
#define GL_NOOP 0x1505
#endif
#ifndef GL_XOR
#define GL_XOR 0x1506
#endif
#ifndef GL_OR
#define GL_OR 0x1507
#endif
#ifndef GL_NOR
#define GL_NOR 0x1508
#endif
#ifndef GL_EQUIV
#define GL_EQUIV 0x1509
#endif
#ifndef GL_OR_REVERSE
#define GL_OR_REVERSE 0x150B
#endif
#ifndef GL_OR_INVERTED
#define GL_OR_INVERTED 0x150D
#endif
#ifndef GL_NAND
#define GL_NAND 0x150E
#endif
#ifndef GL_SET
#define GL_SET 0x150F
#endif
#ifndef GL_COPY
#define GL_COPY 0x1503
#endif
#ifndef GL_COPY_INVERTED
#define GL_COPY_INVERTED 0x150C
#endif
#ifndef GL_CLEAR
#define GL_CLEAR 0x1500
#endif

/* Texture coordinate names */
#ifndef GL_S
#define GL_S 0x2000
#endif
#ifndef GL_T
#define GL_T 0x2001
#endif
#ifndef GL_R
#define GL_R 0x2002
#endif
#ifndef GL_Q
#define GL_Q 0x2003
#endif
#ifndef GL_TEXTURE_GEN_MODE
#define GL_TEXTURE_GEN_MODE 0x2500
#endif
#ifndef GL_OBJECT_PLANE
#define GL_OBJECT_PLANE 0x2501
#endif
#ifndef GL_EYE_PLANE
#define GL_EYE_PLANE 0x2502
#endif

/* Lighting model */
#ifndef GL_LIGHT_MODEL_LOCAL_VIEWER
#define GL_LIGHT_MODEL_LOCAL_VIEWER 0x0B51
#endif
#ifndef GL_LIGHT_MODEL_TWO_SIDE
#define GL_LIGHT_MODEL_TWO_SIDE 0x0B52
#endif
#ifndef GL_SPOT_EXPONENT
#define GL_SPOT_EXPONENT 0x1205
#endif
#ifndef GL_SPOT_CUTOFF
#define GL_SPOT_CUTOFF 0x1206
#endif
#ifndef GL_SPOT_DIRECTION
#define GL_SPOT_DIRECTION 0x1204
#endif

/* Accumulation buffer */
#ifndef GL_ACCUM_BUFFER_BIT
#define GL_ACCUM_BUFFER_BIT 0x00000200
#endif
#ifndef GL_ACCUM
#define GL_ACCUM 0x0100
#endif
#ifndef GL_LOAD
#define GL_LOAD 0x0101
#endif
#ifndef GL_RETURN
#define GL_RETURN 0x0102
#endif
#ifndef GL_MULT
#define GL_MULT 0x0103
#endif
#ifndef GL_ADD
#define GL_ADD 0x0104
#endif

/* Stereo buffer targets */
#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT 0x0402
#endif
#ifndef GL_BACK_RIGHT
#define GL_BACK_RIGHT 0x0403
#endif
#ifndef GL_FRONT_LEFT
#define GL_FRONT_LEFT 0x0400
#endif
#ifndef GL_FRONT_RIGHT
#define GL_FRONT_RIGHT 0x0401
#endif
#ifndef GL_LEFT
#define GL_LEFT 0x0406
#endif
#ifndef GL_RIGHT
#define GL_RIGHT 0x0407
#endif
#ifndef GL_AUX0
#define GL_AUX0 0x0409
#endif

/* Polygon stipple */
#ifndef GL_POLYGON_STIPPLE
#define GL_POLYGON_STIPPLE 0x0B42
#endif

/* Smooth hints */
#ifndef GL_LINE_SMOOTH_HINT
#define GL_LINE_SMOOTH_HINT 0x0C52
#endif
#ifndef GL_POLYGON_SMOOTH_HINT
#define GL_POLYGON_SMOOTH_HINT 0x0C53
#endif

/* Packed depth-stencil type */
#ifndef GL_UNSIGNED_INT_24_8
#define GL_UNSIGNED_INT_24_8 0x84FA
#endif

/* vitaGL declares some functions without const on pointer params.
 * Use function-like macros to cast away const — these won't interfere
 * with &func (used by GLStaticLibrary) since function-like macros only
 * expand when followed by parentheses. */
#define glTexEnvfv(target, pname, params) glTexEnvfv(target, pname, (float*)(params))
#define glTexParameteriv(target, pname, params) glTexParameteriv(target, pname, (int*)(params))
#define glMultiTexCoord2fv(target, v) glMultiTexCoord2fv(target, (float*)(v))
#define glTexCoord2fv(v) glTexCoord2fv((float*)(v))

/* Missing GL functions — stubs that delegate to available vitaGL equivalents */
#ifdef __cplusplus
inline void glLoadMatrixd(const double *m) {
    float f[16];
    for (int i = 0; i < 16; i++) f[i] = static_cast<float>(m[i]);
    glLoadMatrixf(f);
}
inline void glMultMatrixd(const double *m) {
    float f[16];
    for (int i = 0; i < 16; i++) f[i] = static_cast<float>(m[i]);
    glMultMatrixf(f);
}
inline void glVertex4f(float x, float y, float z, float /*w*/) {
    glVertex3f(x, y, z);
}
inline void glVertex4fv(const float *v) {
    glVertex3f(v[0], v[1], v[2]);
}
inline void glTexCoord4f(float s, float t, float /*r*/, float /*q*/) {
    glTexCoord2f(s, t);
}
inline void glTexCoord4fv(const float *v) {
    glTexCoord2f(v[0], v[1]);
}
inline void glTexCoord3f(float s, float t, float /*r*/) {
    glTexCoord2f(s, t);
}
inline void glTexCoord1f(float s) {
    glTexCoord2f(s, 0.0f);
}
inline void glNormal3d(double x, double y, double z) {
    glNormal3f(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
}
inline void glColor3f(float r, float g, float b) {
    glColor4f(r, g, b, 1.0f);
}
inline void glColor3fv(const float *v) {
    glColor4f(v[0], v[1], v[2], 1.0f);
}
inline void glColor3ub(unsigned char r, unsigned char g, unsigned char b) {
    glColor4ub(r, g, b, 255);
}
inline void glOrtho(double l, double r, double b, double t, double n, double f) {
    glOrthof(static_cast<float>(l), static_cast<float>(r),
             static_cast<float>(b), static_cast<float>(t),
             static_cast<float>(n), static_cast<float>(f));
}
inline void glNormal3dv(const double *v) {
    glNormal3f(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
}
inline void glNormal3bv(const signed char *v) {
    glNormal3f(v[0] / 127.0f, v[1] / 127.0f, v[2] / 127.0f);
}
inline void glNormal3sv(const short *v) {
    glNormal3f(v[0] / 32767.0f, v[1] / 32767.0f, v[2] / 32767.0f);
}
inline void glColor3dv(const double *v) {
    glColor4f(static_cast<float>(v[0]), static_cast<float>(v[1]),
              static_cast<float>(v[2]), 1.0f);
}
inline void glColor4dv(const double *v) {
    glColor4f(static_cast<float>(v[0]), static_cast<float>(v[1]),
              static_cast<float>(v[2]), static_cast<float>(v[3]));
}
inline void glRasterPos3f(float /*x*/, float /*y*/, float /*z*/) {}
inline void glDrawPixels(int /*w*/, int /*h*/, unsigned int /*fmt*/,
                         unsigned int /*type*/, const void * /*data*/) {}
inline void glLightf(unsigned int light, unsigned int pname, float param) {
    float v[1] = { param };
    glLightfv(light, pname, v);
}
inline void glLightModeli(unsigned int pname, int param) {
    float v[1] = { static_cast<float>(param) };
    glLightModelfv(pname, v);
}
inline void glGetLightfv(unsigned int /*light*/, unsigned int /*pname*/, float *params) {
    if (params) params[0] = 0.0f;
}
inline void glLineStipple(int /*factor*/, unsigned short /*pattern*/) {}
inline void glLogicOp(unsigned int /*opcode*/) {}
inline void glGetTexLevelParameteriv(unsigned int /*target*/, int /*level*/,
                                     unsigned int /*pname*/, int *params) {
    if (params) *params = 0;
}
inline void glGetTexImage(unsigned int /*target*/, int /*level*/,
                          unsigned int /*format*/, unsigned int /*type*/,
                          void * /*pixels*/) {}
inline void glPolygonStipple(const unsigned char * /*mask*/) {}
inline void glDrawBuffer(unsigned int /*mode*/) {}
inline void glReadBuffer(unsigned int /*mode*/) {}
inline void glClearAccum(float /*r*/, float /*g*/, float /*b*/, float /*a*/) {}
inline void glAccum(unsigned int /*op*/, float /*value*/) {}
inline void glTexGeni(unsigned int /*coord*/, unsigned int /*pname*/, int /*param*/) {}
inline void glTexGenfv(unsigned int /*coord*/, unsigned int /*pname*/, const float * /*params*/) {}
inline void glTexGendv(unsigned int /*coord*/, unsigned int /*pname*/, const double * /*params*/) {}
inline void glTexParameterfv(unsigned int target, unsigned int pname, const float *params) {
    glTexParameterf(target, pname, params[0]);
}
inline void glGetTexParameteriv(unsigned int /*target*/, unsigned int /*pname*/, int *params) {
    if (params) *params = 0;
}
#else
static inline void glLoadMatrixd(const double *m) {
    float f[16]; int i;
    for (i = 0; i < 16; i++) f[i] = (float)m[i];
    glLoadMatrixf(f);
}
static inline void glMultMatrixd(const double *m) {
    float f[16]; int i;
    for (i = 0; i < 16; i++) f[i] = (float)m[i];
    glMultMatrixf(f);
}
static inline void glVertex4f(float x, float y, float z, float w) {
    (void)w; glVertex3f(x, y, z);
}
static inline void glTexCoord4f(float s, float t, float r, float q) {
    (void)r; (void)q; glTexCoord2f(s, t);
}
static inline void glTexCoord3f(float s, float t, float r) {
    (void)r; glTexCoord2f(s, t);
}
static inline void glTexCoord1f(float s) {
    glTexCoord2f(s, 0.0f);
}
static inline void glNormal3d(double x, double y, double z) {
    glNormal3f((float)x, (float)y, (float)z);
}
static inline void glColor3f(float r, float g, float b) {
    glColor4f(r, g, b, 1.0f);
}
static inline void glOrtho(double l, double r, double b, double t, double n, double f) {
    glOrthof((float)l, (float)r, (float)b, (float)t, (float)n, (float)f);
}
#endif

#endif /* _OPENMW_VITA_GL_H_ */
