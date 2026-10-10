#!/bin/bash
# "osg-lite": the OpenSceneGraph headers plus the handful of libosg/OpenThreads
# sources the 3DS game logic actually links (Quat, Matrixf/Matrixd, Timer,
# Stats, Referenced/Observer/DeleteHandler, OpenThreads pthreads). There is no
# scene graph and no GL in this library on purpose: the 3DS renderer is native
# citro3d, so any osg::Node use that creeps back fails to link.
HERE="$(cd "$(dirname "$0")" && pwd)"
source "${HERE}/common.sh"
fetch universe/o/openscenegraph openscenegraph_3.6.5+dfsg1.orig.tar.gz
cd "${CTR_DEPS_WORK}"
rm -rf osg-src osglite-build && mkdir osg-src osglite-build && tar xzf openscenegraph_3.6.5+dfsg1.orig.tar.gz -C osg-src
SRC="$(ls -d osg-src/*)"
INC="${CTR_DEPS_PREFIX}/include"
rm -rf "${INC}/osg" "${INC}/OpenThreads"
cp -r "${SRC}/include/osg" "${SRC}/include/OpenThreads" "${INC}/"

cat > "${INC}/osg/Config" <<'CFG'
#ifndef OSG_CONFIG
#define OSG_CONFIG 1
#define OSG_NOTIFY_DISABLED
#define OSG_USE_FLOAT_MATRIX
#define OSG_USE_FLOAT_PLANE
#define OSG_USE_FLOAT_QUAT
#define OSG_USE_REF_PTR_IMPLICIT_OUTPUT_CONVERSION
#endif
CFG
cat > "${INC}/osg/GL" <<'CFG'
#ifndef OSG_GL
#define OSG_GL 1
/* osg-lite: GL types only, no GL entry points (the 3DS renderer is citro3d). */
#include <osg/Export>
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;
typedef char GLchar;
typedef long GLintptr;
typedef long GLsizeiptr;
typedef unsigned long long GLuint64EXT;
typedef long long GLint64EXT;
#define GL_FALSE 0
#define GL_TRUE 1
#endif
CFG
sed -e 's/@OPENSCENEGRAPH_MAJOR_VERSION@/3/;s/@OPENSCENEGRAPH_MINOR_VERSION@/6/;s/@OPENSCENEGRAPH_PATCH_VERSION@/5/;s/@OPENSCENEGRAPH_SOVERSION@/161/' \
    "${SRC}/src/osg/Version.in" > "${INC}/osg/Version"
cat > "${INC}/OpenThreads/Config" <<'CFG'
#ifndef _OPENTHREADS_CONFIG
#define _OPENTHREADS_CONFIG
#define _OPENTHREADS_ATOMIC_USE_GCC_BUILTINS
#define OT_LIBRARY_STATIC
#endif
CFG
sed -e 's/@OPENTHREADS_MAJOR_VERSION@/3/;s/@OPENTHREADS_MINOR_VERSION@/3/;s/@OPENTHREADS_PATCH_VERSION@/1/;s/@OPENTHREADS_SOVERSION@/21/' \
    "${SRC}/src/OpenThreads/common/Version.in" > "${INC}/OpenThreads/Version"

FLAGS="${CTR_FLAGS} -std=gnu++17 -D_DEFAULT_SOURCE -DOSG_LIBRARY_STATIC -DOT_LIBRARY_STATIC -DOSG_LIBRARY -I${INC}"
for f in osg/Quat osg/Matrixf osg/Matrixd osg/Timer osg/Stats osg/Referenced osg/DeleteHandler \
         osg/Notify osg/Object osg/Math OpenThreads/pthreads/PThread OpenThreads/pthreads/PThreadMutex \
         OpenThreads/pthreads/PThreadCondition OpenThreads/common/Atomic OpenThreads/common/Version; do
    "${CTR_CXX}" ${FLAGS} -I"${SRC}/src/OpenThreads/pthreads" -c "${SRC}/src/${f}.cpp" -o "osglite-build/$(basename "${f}").o"
done
"${CTR_CXX}" ${FLAGS} -c "${HERE}/osglite/osglite.cpp" -o osglite-build/osglite.o
rm -f "${CTR_DEPS_PREFIX}/lib/libosglite.a"
"${CTR_AR}" rcs "${CTR_DEPS_PREFIX}/lib/libosglite.a" osglite-build/*.o
