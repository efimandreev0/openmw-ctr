#!/bin/bash
# Build/install ICU for OpenMW Vita port
# Uses vitasdk/packages if available, otherwise builds from source
# ICU requires a two-stage build: host tools first, then cross-compile
set -e

# Error cleanup handler
trap 'rm -f "${WORKDIR}/icu_done"' ERR

VITASDK="${VITASDK:-/usr/local/vitasdk}"
SYSROOT="${VITASDK}/arm-vita-eabi"

echo "=== Installing ICU for Vita ==="

WORKDIR="${1:-$(pwd)/vita-deps-build/icu}"
ICU_VERSION="73-2"
ICU_VERSION_DASH="73.2"

echo "Building ICU ${ICU_VERSION_DASH} from source..."
echo "Work dir: ${WORKDIR}"

mkdir -p "${WORKDIR}"
cd "${WORKDIR}"

if [ ! -f icu_done ]; then
    if [ ! -f "icu4c-${ICU_VERSION}-src.tgz" ]; then
        wget -q -L "https://github.com/unicode-org/icu/releases/download/release-${ICU_VERSION}/icu4c-${ICU_VERSION//-/_}-src.tgz" -O "icu4c-${ICU_VERSION}-src.tgz"
    fi

    tar xf "icu4c-${ICU_VERSION}-src.tgz"

    # Stage 1: Build host tools (needed for data generation)
    echo "--- Stage 1: Building host ICU tools ---"
    mkdir -p build-host
    cd build-host
    ../icu/source/runConfigureICU Linux \
        --disable-shared \
        --enable-static \
        --disable-extras \
        --disable-tests \
        --disable-samples
    make -j$(nproc | awk '{print ($1 > 8) ? 8 : $1}')
    cd ..

    # Stage 2: Cross-compile for Vita
    echo "--- Stage 2: Cross-compiling ICU for Vita ---"
    mkdir -p build-vita
    cd build-vita

    # ICU doesn't recognize arm-vita-eabi — use Linux platform config
    cp ../icu/source/config/mh-linux ../icu/source/config/mh-unknown

    CC="${VITASDK}/bin/arm-vita-eabi-gcc" \
    CXX="${VITASDK}/bin/arm-vita-eabi-g++" \
    AR="${VITASDK}/bin/arm-vita-eabi-ar" \
    RANLIB="${VITASDK}/bin/arm-vita-eabi-ranlib" \
    CFLAGS="-Os -mcpu=cortex-a9 -mfpu=neon -fno-PIC -ffunction-sections -fdata-sections -fvisibility=hidden" \
    CXXFLAGS="-Os -mcpu=cortex-a9 -mfpu=neon -fno-PIC -ffunction-sections -fdata-sections -fvisibility=hidden" \
    ../icu/source/configure \
        --host=arm-vita-eabi \
        --prefix="${SYSROOT}" \
        --with-cross-build="$(pwd)/../build-host" \
        --enable-static \
        --disable-shared \
        --disable-extras \
        --disable-strict \
        --disable-icuio \
        --disable-layout \
        --disable-layoutex \
        --disable-tools \
        --disable-tests \
        --disable-samples \
        --disable-dyload \
        --with-data-packaging=archive

    make -j$(nproc | awk '{print ($1 > 8) ? 8 : $1}')
    make install

    # Validate installation
    if [ ! -f "${SYSROOT}/lib/libicuuc.a" ] || [ ! -f "${SYSROOT}/lib/libicui18n.a" ] || [ ! -f "${SYSROOT}/lib/libicudata.a" ]; then
        echo "ERROR: ICU installation failed - libraries not found in ${SYSROOT}/lib"
        exit 1
    fi

    cd ..
    touch icu_done
fi

echo "=== ICU installation complete ==="
