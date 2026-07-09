#!/bin/bash
# Build/install FFmpeg for OpenMW 3DS (CTR) port — devkitARM
# Mirrors scripts/vita-deps/build-ffmpeg.sh; minimal decoder set for Morrowind
set -e

trap 'rm -f "${WORKDIR}/ffmpeg_done"' ERR

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-${DEVKITPRO}/devkitARM}"
SYSROOT="${DEVKITPRO}/portlibs/3ds"

echo "=== Installing FFmpeg for 3DS ==="

WORKDIR="${1:-$(pwd)/ctr-deps-build/ffmpeg}"
FFMPEG_VERSION="6.1"

mkdir -p "${WORKDIR}"
cd "${WORKDIR}"

if [ ! -f ffmpeg_done ]; then
    if [ ! -f "ffmpeg-${FFMPEG_VERSION}.tar.bz2" ]; then
        wget -q "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2"
    fi

    rm -rf "ffmpeg-${FFMPEG_VERSION}"
    tar xf "ffmpeg-${FFMPEG_VERSION}.tar.bz2"
    cd "ffmpeg-${FFMPEG_VERSION}"

    # ARM11 MPCore = ARMv6K, no Thumb-2, no NEON, VFPv2.
    # devkitARM has working pthreads (gthr posix model).
    ./configure \
        --prefix="${SYSROOT}" \
        --cross-prefix="${DEVKITARM}/bin/arm-none-eabi-" \
        --arch=armv6 \
        --target-os=none \
        --enable-cross-compile \
        --enable-static \
        --disable-shared \
        --disable-programs \
        --disable-doc \
        --disable-network \
        --disable-avfilter \
        --disable-avdevice \
        --disable-armv6t2 \
        --disable-neon \
        --disable-vfpv3 \
        --disable-everything \
        --enable-avcodec \
        --enable-avformat \
        --enable-avutil \
        --enable-swscale \
        --enable-swresample \
        --enable-decoder=mp3 \
        --enable-decoder=aac \
        --enable-decoder=pcm_s16le \
        --enable-decoder=pcm_u8 \
        --enable-decoder=vorbis \
        --enable-decoder=bink \
        --enable-decoder=binkaudio_rdft \
        --enable-decoder=binkaudio_dct \
        --enable-decoder=wav \
        --enable-demuxer=mp3 \
        --enable-demuxer=aac \
        --enable-demuxer=ogg \
        --enable-demuxer=wav \
        --enable-demuxer=pcm_s16le \
        --enable-demuxer=bink \
        --enable-parser=mpegaudio \
        --enable-parser=aac \
        --enable-parser=vorbis \
        --enable-protocol=file \
        --enable-pthreads \
        --extra-cflags="-O2 -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -D__3DS__ -fomit-frame-pointer -Wno-incompatible-pointer-types" \
        --extra-ldflags="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft"

    make -j$(nproc | awk '{print ($1 > 8) ? 8 : $1}')
    make install

    if [ ! -f "${SYSROOT}/lib/libavcodec.a" ] || [ ! -f "${SYSROOT}/lib/libavformat.a" ] || [ ! -f "${SYSROOT}/lib/libavutil.a" ]; then
        echo "ERROR: FFmpeg installation failed - libraries not found in ${SYSROOT}/lib"
        exit 1
    fi

    cd ..
    touch ffmpeg_done
fi

echo "=== FFmpeg installation complete ==="
