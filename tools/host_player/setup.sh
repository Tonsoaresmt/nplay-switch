#!/bin/bash
# Prepara o ambiente host (Linux) que roda o player REAL do NRO (player.c,
# curl_avio.c, player_ui.c...) com FFmpeg 7.1, libcurl e SDL2 offscreen.
# - FFmpeg 7.1 (libavformat 61.7.100, o mesmo do Switch): bibliotecas do wheel
#   PyAV 14.0.1 (PyPI) + headers do port Switch em $DEVKITPRO/portlibs/switch.
# - Pacotes apt: libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev
#   libcurl4-openssl-dev ffmpeg (CLI so para gerar os conteudos de teste).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${HOST_PLAYER_WORK:-$HERE/../../build/host_player}
DKP=${DEVKITPRO:-/opt/devkitpro}
mkdir -p "$WORK/inc" "$WORK/lib" "$WORK/gen" "$WORK/wheel"
if [ ! -d "$WORK/wheel/av.libs" ]; then
    pip download --no-deps --only-binary=:all: --platform manylinux_2_17_x86_64 \
        --python-version 3.12 -d "$WORK/wheel" "av==14.0.1"
    (cd "$WORK/wheel" && python3 -m zipfile -e av-14.0.1-*.whl .)
fi
for d in libavformat libavcodec libavutil libswscale libswresample; do
    ln -sfn "$DKP/portlibs/switch/include/$d" "$WORK/inc/$d"
done
for f in "$WORK"/wheel/av.libs/*.so*; do ln -sf "$f" "$WORK/lib/$(basename "$f")"; done
cd "$WORK/lib"
ln -sf libavformat-*.so.61* libavformat.so; ln -sf libavcodec-*.so.61* libavcodec.so
ln -sf libavutil-*.so.59* libavutil.so; ln -sf libswscale-*.so.8* libswscale.so
ln -sf libswresample-*.so.5* libswresample.so
# Assets embutidos (bin2o do devkitPro) como arrays C.
for b in cacert pipoca_atlas brand; do
    python3 - "$HERE/../../data/$b.bin" "$b" "$WORK/gen" <<'PY'
import sys
path, name, out = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(path, 'rb').read()
open(f"{out}/{name}_bin.h", "w").write(
    f"#pragma once\n#include <stdint.h>\nextern const uint8_t {name}_bin[];\n"
    f"extern const uint8_t {name}_bin_end[];\nextern const uint32_t {name}_bin_size;\n")
with open(f"{out}/{name}_bin.c", "w") as f:
    f.write(f"#include <stdint.h>\nconst uint8_t {name}_bin[{len(data) + 1}] __attribute__((aligned(4))) = {{"
            + ",".join(str(b) for b in data) + ",0};\n")
    f.write(f"const uint32_t {name}_bin_size = {len(data)};\n")
    f.write(f'__asm__(".globl {name}_bin_end\\n.set {name}_bin_end, {name}_bin+{len(data)}");\n')
PY
done
# Certificado do servidor HTTPS local (endpoints da API exigem https://).
[ -f "$WORK/test.crt" ] || openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORK/test.key" \
    -out "$WORK/test.crt" -days 365 -subj "/CN=127.0.0.1" \
    -addext "subjectAltName=IP:127.0.0.1,DNS:localhost" 2>/dev/null
echo "ambiente pronto em $WORK"
