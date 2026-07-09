#!/bin/bash
# Build/install LuaJIT for OpenMW Vita port
# Uses vitasdk/packages if available, otherwise builds from source
# Note: JIT is disabled on Vita (console W^X restriction), but the fast
# interpreter is still significantly faster than standard Lua 5.1.
set -e

# Error cleanup handler
trap 'rm -f "${WORKDIR}/luajit_done"' ERR

VITASDK="${VITASDK:-/usr/local/vitasdk}"
SYSROOT="${VITASDK}/arm-vita-eabi"

echo "=== Installing LuaJIT for Vita ==="

WORKDIR="${1:-$(pwd)/vita-deps-build/luajit}"

echo "Building LuaJIT from source..."
echo "Work dir: ${WORKDIR}"

mkdir -p "${WORKDIR}"
cd "${WORKDIR}"

if [ ! -f luajit_done ]; then
    if [ ! -d "LuaJIT-Vita" ]; then
        # Lock to specific commit (2022-08-19) - last known working version
        # Commit c329ddd: "Update README.md"
        git clone https://github.com/SonicMastr/LuaJIT-Vita.git
        cd LuaJIT-Vita
        git checkout c329ddd10691c1875f26087ba23c2ae278515e24
        cd ..
    fi

    cd LuaJIT-Vita/src

    make clean || true
    make \
        HOST_CC="gcc -m32" \
        CROSS=arm-vita-eabi- \
        TARGET_SYS=PSP2 \
        TARGET_FLAGS="-marm -fno-optimize-sibling-calls" \
        BUILDMODE=static \
        XCFLAGS="-DLUAJIT_DISABLE_JIT -DLUAJIT_DISABLE_FFI" \
        -j$(nproc | awk '{print ($1 > 8) ? 8 : $1}')

    # Install headers and library
    cp luajit.h lua.h lualib.h lauxlib.h lua.hpp luaconf.h lj_arch.h "${SYSROOT}/include/"
    cp libluajit.a "${SYSROOT}/lib/"

    # Validate installation
    if [ ! -f "${SYSROOT}/lib/libluajit.a" ]; then
        echo "ERROR: LuaJIT installation failed - libluajit.a not found in ${SYSROOT}/lib"
        exit 1
    fi

    cd ../..
    touch luajit_done
fi

echo "=== LuaJIT installation complete ==="
