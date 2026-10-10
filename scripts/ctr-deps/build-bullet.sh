#!/bin/bash
# Bullet 3.24: only LinearMath + BulletCollision (OpenMW never uses the dynamics
# world). Built from the unity sources; single precision unless
# CTR_BULLET_DOUBLE=1 (must match BT_USE_DOUBLE_PRECISION in the OpenMW build).
source "$(dirname "$0")/common.sh"
fetch universe/b/bullet bullet_3.24+dfsg.orig.tar.xz
cd "${CTR_DEPS_WORK}"
rm -rf bullet-src bullet-build && mkdir bullet-src bullet-build && tar xJf bullet_3.24+dfsg.orig.tar.xz -C bullet-src
SRC="$(ls -d bullet-src/*/src)"
EXTRA="-DBT_NO_PROFILE=1"
[ "${CTR_BULLET_DOUBLE:-0}" = 1 ] && EXTRA="${EXTRA} -DBT_USE_DOUBLE_PRECISION"
for unit in btLinearMathAll btBulletCollisionAll; do
    "${CTR_CXX}" ${CTR_FLAGS} ${EXTRA} -std=gnu++17 -I"${SRC}" -c "${SRC}/${unit}.cpp" -o "bullet-build/${unit}.o"
done
"${CTR_AR}" rcs "${CTR_DEPS_PREFIX}/lib/libLinearMath.a" bullet-build/btLinearMathAll.o
"${CTR_AR}" rcs "${CTR_DEPS_PREFIX}/lib/libBulletCollision.a" bullet-build/btBulletCollisionAll.o
mkdir -p "${CTR_DEPS_PREFIX}/include/bullet"
(cd "${SRC}" && find LinearMath BulletCollision -name '*.h' -exec cp --parents {} "${CTR_DEPS_PREFIX}/include/bullet/" \; \
    && cp btBulletCollisionCommon.h "${CTR_DEPS_PREFIX}/include/bullet/")
