#!/bin/bash
# Build/install FFmpeg for OpenMW Vita port
# Uses vitasdk/packages if available, otherwise builds from source
# Minimal config: only decoders needed for Morrowind audio/video
set -e

# Error cleanup handler
trap 'rm -f "${WORKDIR}/ffmpeg_done"' ERR

VITASDK="${VITASDK:-/usr/local/vitasdk}"
SYSROOT="${VITASDK}/arm-vita-eabi"

echo "=== Installing FFmpeg for Vita ==="

WORKDIR="${1:-$(pwd)/vita-deps-build/ffmpeg}"
FFMPEG_VERSION="6.1"

echo "Building FFmpeg ${FFMPEG_VERSION} from source..."
echo "Work dir: ${WORKDIR}"

mkdir -p "${WORKDIR}"
cd "${WORKDIR}"

if [ ! -f ffmpeg_done ]; then
    if [ ! -f "ffmpeg-${FFMPEG_VERSION}.tar.bz2" ]; then
        wget -q "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2"
    fi

    tar xf "ffmpeg-${FFMPEG_VERSION}.tar.bz2"
    cd "ffmpeg-${FFMPEG_VERSION}"

    ./configure \
        --prefix="${SYSROOT}" \
        --cross-prefix="${VITASDK}/bin/arm-vita-eabi-" \
        --arch=armv7-a \
        --cpu=cortex-a9 \
        --target-os=none \
        --enable-cross-compile \
        --enable-static \
        --disable-shared \
        --disable-programs \
        --disable-doc \
        --disable-network \
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
        --extra-cflags="-Os -mcpu=cortex-a9 -mfpu=neon -ftree-vectorize -fomit-frame-pointer"

    make -j$(nproc | awk '{print ($1 > 8) ? 8 : $1}')
    make install

    # Validate installation
    if [ ! -f "${SYSROOT}/lib/libavcodec.a" ] || [ ! -f "${SYSROOT}/lib/libavformat.a" ] || [ ! -f "${SYSROOT}/lib/libavutil.a" ]; then
        echo "ERROR: FFmpeg installation failed - libraries not found in ${SYSROOT}/lib"
        exit 1
    fi

    cd ..
    touch ffmpeg_done
fi

echo "=== FFmpeg installation complete ==="
