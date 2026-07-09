#!/bin/bash
# Install PVR_PSP2 (PowerVR GPU driver headers + stubs) for Vita
# Required by vitaGL for GPU access
set -e

# Error cleanup handler
trap 'rm -f "${WORKDIR}/pvr_stubs_done" "${WORKDIR}/pvr_headers_done"' ERR

VITASDK="${VITASDK:-/usr/local/vitasdk}"
SYSROOT="${VITASDK}/arm-vita-eabi"
WORKDIR="${1:-$(pwd)/vita-deps-build/pvr-psp2}"

echo "=== Installing PVR_PSP2 ==="
echo "VitaSDK: ${VITASDK}"
echo "Work dir: ${WORKDIR}"

mkdir -p "${WORKDIR}"
cd "${WORKDIR}"

PVR_VERSION="v3.9"
echo "--- Installing PVR_PSP2 ${PVR_VERSION} ---"

if [ ! -f pvr_headers_done ]; then
    wget -q "https://github.com/GrapheneCt/PVR_PSP2/archive/refs/tags/${PVR_VERSION}.tar.gz" -O pvr_src.tar.gz
    tar xf pvr_src.tar.gz
    cp -r PVR_PSP2-*/include/* "${SYSROOT}/include/"
    touch pvr_headers_done
    echo "PVR_PSP2 headers installed"
fi

if [ ! -f pvr_stubs_done ]; then
    wget -q "https://github.com/GrapheneCt/PVR_PSP2/releases/download/${PVR_VERSION}/vitasdk_stubs.zip" -O pvr_stubs.zip
    unzip -o pvr_stubs.zip -d "${SYSROOT}/lib/"
    touch pvr_stubs_done
    echo "PVR_PSP2 stubs installed"
fi

# Validate installation
if [ ! -d "${SYSROOT}/include/gpu_es4" ] || [ ! -d "${SYSROOT}/lib/libGLESv2_stub_vitasdk.a" ]; then
    echo "ERROR: PVR_PSP2 installation failed - headers or stubs not found in ${SYSROOT}"
    exit 1
fi

echo "=== PVR_PSP2 installation complete ==="
