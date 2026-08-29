#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="${ROOT_DIR}/.build-thirdparty/ffmpeg"
SOURCE_DIR="${WORK_DIR}/ffmpeg-7.1"
VENDOR_LIB="${ROOT_DIR}/vendor/ffmpeg-https/lib"
FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz"
PORT_URL="https://raw.githubusercontent.com/devkitPro/pacman-packages/master/switch/ffmpeg"

mkdir -p "${WORK_DIR}" "${VENDOR_LIB}"

download() {
    local output="$1"
    local url="$2"
    if [[ ! -f "${output}" ]]; then
        curl -L --fail --retry 3 -o "${output}" "${url}"
    fi
}

download "${WORK_DIR}/ffmpeg-7.1.tar.xz" "${FFMPEG_URL}"
download "${WORK_DIR}/ffmpeg-7.1.patch" "${PORT_URL}/ffmpeg-7.1.patch"
download "${WORK_DIR}/tls.patch" "${PORT_URL}/tls.patch"

echo "40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6  ${WORK_DIR}/ffmpeg-7.1.tar.xz" | sha256sum -c -
echo "1792380b992e3554a4abcddf0d7b395bfd8c118ac7c6e38c8f2fb0d39753a390  ${WORK_DIR}/ffmpeg-7.1.patch" | sha256sum -c -
echo "57ea7ec8ed26d13d3172d0fd589b7883d22f0c180d50b7434fcc73fb2b3ab7d7  ${WORK_DIR}/tls.patch" | sha256sum -c -

rm -rf "${SOURCE_DIR}"
tar -C "${WORK_DIR}" -xf "${WORK_DIR}/ffmpeg-7.1.tar.xz"
cd "${SOURCE_DIR}"

PATCH_BIN="$(command -v patch || true)"
if [[ -z "${PATCH_BIN}" && -x '/c/Program Files/Git/usr/bin/patch.exe' ]]; then
    PATCH_BIN='/c/Program Files/Git/usr/bin/patch.exe'
fi
if [[ -z "${PATCH_BIN}" ]]; then
    echo "Erro: utilitario patch nao encontrado (MSYS2 ou Git for Windows)." >&2
    exit 1
fi

"${PATCH_BIN}" -Np1 -i "${WORK_DIR}/ffmpeg-7.1.patch"
"${PATCH_BIN}" -Np1 -i "${WORK_DIR}/tls.patch"
"${PATCH_BIN}" -Np1 -i "${ROOT_DIR}/tools/ffmpeg-libnx-tls-hostname.patch"

if [[ -f /opt/devkitpro/switchvars.sh ]]; then
    source /opt/devkitpro/switchvars.sh
else
    export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
    export DEVKITA64="${DEVKITA64:-${DEVKITPRO}/devkitA64}"
    export PORTLIBS_PREFIX="${PORTLIBS_PREFIX:-${DEVKITPRO}/portlibs/switch}"
    export PATH="${DEVKITA64}/bin:${DEVKITPRO}/tools/bin:${PORTLIBS_PREFIX}/bin:${PATH}"
    export PKG_CONFIG_PATH="${PORTLIBS_PREFIX}/lib/pkgconfig"
fi

./configure --prefix="${PORTLIBS_PREFIX}" --enable-gpl --disable-shared --enable-static \
    --cross-prefix=aarch64-none-elf- --enable-cross-compile \
    --arch=aarch64 --cpu=cortex-a57 --target-os=horizon --enable-pic \
    --extra-cflags="-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec -I${PORTLIBS_PREFIX}/include -I${DEVKITPRO}/libnx/include" \
    --extra-cxxflags="-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec -I${PORTLIBS_PREFIX}/include -I${DEVKITPRO}/libnx/include" \
    --extra-ldflags='-fPIE -L${PORTLIBS_PREFIX}/lib -L${DEVKITPRO}/libnx/lib' \
    --disable-runtime-cpudetect --disable-programs --disable-debug --disable-doc --disable-autodetect \
    --enable-asm --enable-neon \
    --disable-avdevice --disable-encoders --disable-muxers \
    --enable-swscale --enable-swresample --enable-network \
    --disable-protocols --enable-protocol=file,http,https,ftp,tcp,udp,rtmp,tls,httpproxy \
    --enable-zlib --enable-bzlib --enable-libass --enable-libfreetype --enable-libfribidi --enable-libdav1d \
    --enable-libnx --enable-nvtegra

make -j"${JOBS:-4}" libavformat/libavformat.a
cp libavformat/libavformat.a "${VENDOR_LIB}/libavformat.a"

if ! aarch64-none-elf-nm "${VENDOR_LIB}/libavformat.a" | grep -q 'ff_https_protocol'; then
    echo "Erro: a biblioteca gerada nao contem ff_https_protocol." >&2
    exit 1
fi

if ! grep -q 'SslVerifyOption_HostName' libavformat/tls_libnx.c; then
    echo "Erro: validacao de hostname nao foi aplicada ao TLS libnx." >&2
    exit 1
fi

echo "libavformat com HTTPS gerada em ${VENDOR_LIB}/libavformat.a"
