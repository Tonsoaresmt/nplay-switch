#!/bin/bash
# Roteiro de regressao do player real. Uso: suite.sh [atraso_ms jitter_ms mbps]
# Ex.: suite.sh 200 100 30  (rede boa)   |   suite.sh 500 250 10  (Wi-Fi ruim)
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/../.." && pwd)
WORK=${HOST_PLAYER_WORK:-$REPO/build/host_player}; BIN=${HARNESS_BIN:-$WORK/bin/harness}
F=${HOST_PLAYER_FIXTURES:-$WORK/fixtures}; D=${1:-200}; J=${2:-100}; M=${3:-30}
SID=hotsid123456789012345678901234567890; RUN=$WORK/run; mkdir -p "$RUN/sdmc:/switch"; cd "$RUN"
start_server() { # porta [tls]
    pkill -f "^python3 .*latency_[s]erver.py --root .* --port $1 " 2>/dev/null; sleep 0.3
    nohup python3 "$HERE/latency_server.py" --root "$F" --port "$1" ${2:+--tls "$WORK/test"} \
        --delay-ms "$D" --jitter-ms "$J" --mbps "$M" --sub-kbps 2 --log "$RUN/server_$1.log" >/dev/null 2>&1 &
}
start_server 8765; start_server 8443 tls
# Falha simulada so no 1o pedido da legenda (R2 e torrent) para provar a nova tentativa.
start_fail() { # porta padrao [tls]
    pkill -f "^python3 .*latency_[s]erver.py --root .* --port $1 " 2>/dev/null; sleep 0.3
    nohup python3 "$HERE/latency_server.py" --root "$F" --port "$1" ${3:+--tls "$WORK/test"} \
        --delay-ms "$D" --jitter-ms "$J" --mbps "$M" --sub-kbps 2 --fail-match "$2" --fail-from 1 \
        --fail-count 1 --log "$RUN/server_$1.log" >/dev/null 2>&1 &
}
start_fail 8766 subtitle-0.vtt; start_fail 8444 /subtitles/ tls; sleep 1
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LD_LIBRARY_PATH=$WORK/lib SCRIPT_AFTER_FRAME=1
declare -A RUN_STATUS
run() {
    local name=$1; shift
    env "$@" timeout 120 "$BIN" "$URL" "$CT" > "suite_$name.log" 2>&1
    RUN_STATUS[$name]=$?
}
FAILS=0
check() { # nome descricao padrao  (!padrao = nao pode existir; @nal = NAL invalido so apos sair)
    local f="suite_$1.log" pat=$3 ok=
    # Absence of an error is not success if the process crashed, timed out,
    # never rendered a frame, or never reached its final result/summary.
    if [[ ${RUN_STATUS[$1]:-1} != 0 ]] || ! grep -qE '^SUMMARY video_frames=[1-9][0-9]* ' "$f" || ! grep -q 'RESULT chosen=' "$f"; then ok=
    elif [[ $pat == @nal ]]; then awk '/INJECT MINUS/{m=1} /Invalid NAL/ && !m {bad=1} END{exit bad}' "$f" && ok=1
    elif [[ $pat == !* ]]; then ! grep -qE "${pat:1}" "$f" && ok=1
    else grep -qE "$pat" "$f" && ok=1; fi
    [ "$ok" ] || FAILS=$((FAILS + 1))
    printf "%-6s %-8s %s\n" "$([ "$ok" ] && echo PASSA || echo FALHA)" "$1" "$2"
}
URL=http://127.0.0.1:8765/r2/index.m3u8 CT=m3u8
run start SCRIPT="3000:A,5000:A,10000:MINUS"
check start "R2 abre e mostra video" "render/first-present position=0"
check start "pausa e retoma" "controls/resume"
run resume START=66 AUDIO_PREF=1 SCRIPT="8000:MINUS"
check resume "Continuar em 66 s mostra quadro perto de 66 s" "first-present position=6[5-7]\."
check resume "sem NAL invalido antes de sair" "@nal"
run seek AUDIO_PREF=1 SCRIPT="3000:R,3300:R,3600:R,9000:ZR,15000:L,21000:MINUS"
check seek "R x3 soma em um unico salto (+30 s)" "quick-commit from=[0-9.]+ target=3[0-9]"
check seek "ZR mostra quadro perto de +60 s" "first-present position=(9[0-9]|10[0-9])\."
check seek "L volta e mostra quadro" "kind=seek generation=3"
check seek "legenda continua apos os saltos" "!manifest-load-fail"
check seek "sem NAL invalido antes de sair" "@nal"
run audio AUDIO_PREF=1 SCRIPT="3000:Y,4000:DOWN,4500:A,11000:MINUS"
check audio "troca japones -> portugues" "streams/selected .*lang=pt .*priority=2"
run next HAS_NEXT=1 SCRIPT="2000:RIGHT,3000:A"
check next "proximo episodio pelo player" "RESULT chosen=0 rc=4 reason=3"
run eps EPISODES=12 SCRIPT="2000:LEFT,3000:B,5000:LEFT,5500:UP,6000:UP,6500:A"
check eps "painel Episodios escolhe o anterior" "RESULT chosen=5000 rc=4 reason=3"
URL=http://127.0.0.1:8765/anime.mp4 CT=mp4
run mp4 SCRIPT="3000:R,3300:R,9000:L,15000:A,17000:A,21000:MINUS"
check mp4 "MP4: salto acumulado e aplicado" "seek/resume-frame wanted=2[0-9]"
check mp4 "MP4: pausa/retoma" "controls/resume"
URL=http://127.0.0.1:8765/api/media/hot/$SID/video CT=mp4
run hot EXTRA_CA=$WORK/test.crt BASE_URL=https://127.0.0.1:8443 DELIVERY=hot SEQUENTIAL=1 HOT_SID=$SID AUDIO_PREF=1 SCRIPT="28000:MINUS"
check hot "torrent: legenda carregada" "hot-probe tracks=1"
check hot "torrent: duracao da sonda (360 s)" "sequential-duration demuxer=[0-9.]+ probe=360"
check hot "torrent: sem cortes de audio" "underruns=0"
check hot "torrent: progresso com duracao real" "API progress item=4242 pos=[0-9]+ dur=360"
URL=http://127.0.0.1:8765/r2sub/index.m3u8 CT=m3u8
run subkara START=150 AUDIO_PREF=1 SCRIPT="6000:MINUS"
check subkara "legenda com karaoke pesado: fala depois de 8192 eventos" "SUB pos=.*Fala 31 \\(150s\\)"
check subkara "legenda carregada sem limite de eventos" "subtitle/loaded cues=[0-9]{5}"
run subkara2 START=30 AUDIO_PREF=1 SCRIPT="7000:MINUS"
check subkara2 "karaoke nao vira lixo na tela (so a fala)" "!SUB pos=.*Fala [0-9]+ \\([0-9]+s\\) / "
URL=http://127.0.0.1:8766/r2sub/index.m3u8 CT=m3u8
run subretry START=150 AUDIO_PREF=1 SCRIPT="12000:MINUS"
check subretry "legenda R2 volta depois de falha de rede" "subtitle/retry choice=1"
check subretry "fala aparece depois da nova tentativa" "SUB pos=.*Fala"
run subseek START=150 AUDIO_PREF=1 SCRIPT="9000:ZR,16000:MINUS"
check subseek "salto reaproveita a legenda (sem novo download)" "subtitle/session-reuse"
URL=http://127.0.0.1:8765/api/media/hot/$SID/video CT=mp4
run hotretry EXTRA_CA=$WORK/test.crt BASE_URL=https://127.0.0.1:8444 DELIVERY=hot SEQUENTIAL=1 HOT_SID=$SID AUDIO_PREF=1 SCRIPT="14000:MINUS"
check hotretry "legenda do torrent volta depois de falha" "subtitle/stream-retry"
check hotretry "fala do torrent aparece" "SUB pos=.*Fala"
for f in suite_*.log; do echo "$f: $(grep -h SUMMARY "$f" | sed 's/SUMMARY //')"; done
pkill -f "^python3 .*latency_[s]erver.py --root $F " 2>/dev/null
exit $FAILS
