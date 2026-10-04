#!/bin/bash
# Compila o harness com o codigo do repositorio. Uso: build.sh [saida]
set -e
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/../.." && pwd)
WORK=${HOST_PLAYER_WORK:-$REPO/build/host_player}; OUT=${1:-$WORK/bin}
CF="-std=gnu11 -O1 -g -w -DAPP_VERSION_STR=\"harness\" -I $HERE/stubs -I $WORK/gen -I $WORK/inc -I $REPO/include -I $REPO/source $(sdl2-config --cflags)"
mkdir -p "$OUT"
for f in player player_ui text diag curl_avio net store ui audio_policy hls_manifest subtitle_queue \
         player_clock player_loading player_next player_sync hot_subtitles vtt_stream touch_input cJSON api; do
    gcc $CF -c -o "$OUT/$f.o" "$REPO/source/$f.c"
done
for f in "$WORK"/gen/*.c "$HERE/stubs/nx_stubs.c" "$HERE/harness_main.c"; do
    gcc $CF -c -o "$OUT/$(basename "$f" .c).o" "$f"
done
W="-Wl,--wrap=SDL_UpdateYUVTexture,--wrap=SDL_UpdateNVTexture,--wrap=SDL_UpdateTexture,--wrap=SDL_RenderPresent,--wrap=SDL_PauseAudioDevice,--wrap=diag_player_event,--wrap=diag_player_begin"
gcc -o "$OUT/harness" "$OUT"/*.o $W -L "$WORK/lib" -Wl,-rpath,"$WORK/lib" -lavformat -lavcodec \
    -lavutil -lswscale -lswresample $(sdl2-config --libs) -lSDL2_ttf -lSDL2_image -lcurl -lm -lpthread
echo "harness: $OUT/harness"
