#!/bin/bash
# Build/install Boost for OpenMW Vita port
# Uses vitasdk/packages if available, otherwise builds from source
set -e

# Error cleanup handler
trap 'rm -f "${WORKDIR}/boost_done"' ERR

VITASDK="${VITASDK:-/usr/local/vitasdk}"
SYSROOT="${VITASDK}/arm-vita-eabi"

echo "=== Installing Boost for Vita ==="

WORKDIR="${1:-$(pwd)/vita-deps-build/boost}"
BOOST_VERSION="1.78.0"
BOOST_VERSION_UNDERSCORE="1_78_0"

echo "Building Boost ${BOOST_VERSION} from source..."
echo "Work dir: ${WORKDIR}"

mkdir -p "${WORKDIR}"
cd "${WORKDIR}"

if [ ! -f boost_done ]; then
    # Download
    if [ ! -f "boost_${BOOST_VERSION_UNDERSCORE}.tar.bz2" ]; then
        wget -q "https://archives.boost.io/release/${BOOST_VERSION}/source/boost_${BOOST_VERSION_UNDERSCORE}.tar.bz2"
    fi

    tar xf "boost_${BOOST_VERSION_UNDERSCORE}.tar.bz2"
    cd "boost_${BOOST_VERSION_UNDERSCORE}"

    # Create user-config.jam for Vita cross-compilation
    cat > user-config.jam << JAMEOF
using gcc : vita : ${VITASDK}/bin/arm-vita-eabi-g++ :
    <compileflags>"-Os -mcpu=cortex-a9 -mfpu=neon -ffunction-sections -fdata-sections -fvisibility=hidden -DBOOST_IOSTREAMS_NO_MAPPED_FILE"
;
JAMEOF

    # Vita has no mmap — stub out mapped_file so it compiles as empty
    echo "// Vita: no mmap support" > libs/iostreams/src/mapped_file.cpp

    # Bootstrap b2
    ./bootstrap.sh

    # Build only the libraries OpenMW needs
    # Limit to 8 jobs to avoid OOM during linking
    ./b2 \
        --user-config=user-config.jam \
        toolset=gcc-vita \
        target-os=linux \
        runtime-link=static \
        link=static \
        threading=multi \
        variant=release \
        --with-iostreams \
        --with-program_options \
        --prefix="${SYSROOT}" \
        -j$(nproc | awk '{print ($1 > 8) ? 8 : $1}') \
        install

    # Validate installation
    if [ ! -f "${SYSROOT}/lib/libboost_iostreams.a" ] || [ ! -f "${SYSROOT}/lib/libboost_program_options.a" ]; then
        echo "ERROR: Boost installation failed - libraries not found in ${SYSROOT}/lib"
        exit 1
    fi

    cd ..
    touch boost_done
fi

echo "=== Boost installation complete ==="
