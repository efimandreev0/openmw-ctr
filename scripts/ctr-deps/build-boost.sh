#!/bin/bash
# Boost 1.83 headers + program_options (the only compiled Boost library OpenMW
# uses: command line / openmw.cfg parsing).
source "$(dirname "$0")/common.sh"
fetch main/b/boost1.83 boost1.83_1.83.0.orig.tar.xz
cd "${CTR_DEPS_WORK}"
rm -rf boost-src boost-build && mkdir boost-src boost-build && tar xJf boost1.83_1.83.0.orig.tar.xz -C boost-src
for d in boost-src/boost/libs/*/include boost-src/boost/libs/numeric/*/include; do
    [ -d "${d}/boost" ] && cp -r "${d}/boost" "${CTR_DEPS_PREFIX}/include/"
done
for f in boost-src/boost/libs/program_options/src/*.cpp; do
    n="$(basename "${f}" .cpp)"
    [ "${n}" = winmain ] && continue
    "${CTR_CXX}" ${CTR_FLAGS} -std=gnu++20 -D_DEFAULT_SOURCE -DBOOST_ALL_NO_LIB -I"${CTR_DEPS_PREFIX}/include" \
        -c "${f}" -o "boost-build/${n}.o"
done
rm -f "${CTR_DEPS_PREFIX}/lib/libboost_program_options.a"
"${CTR_AR}" rcs "${CTR_DEPS_PREFIX}/lib/libboost_program_options.a" boost-build/*.o
