# Player real no host (Linux)

Roda o `player_run` **real** do NRO (`player.c`, `curl_avio.c`, `player_ui.c`,
`hls_manifest.c`...) no Linux, com o **mesmo FFmpeg 7.1** do Switch
(libavformat 61.7.100), libcurl e SDL2 sem tela (`offscreen`) e audio `dummy`
consumido em tempo real. Botoes do Joy-Con sao injetados por roteiro e cada
quadro, pausa de audio, evento do trace e requisicao de rede fica registrado
com o tempo.

Serve para reproduzir e medir travamentos, seeks, troca de faixa, legendas e
proximo episodio antes de levar ao console. **Nao substitui o hardware**:
nao exercita NVTEGRA, o driver de audio do Switch nem o Wi-Fi do console.

## Preparar (uma vez)

```sh
sudo apt-get install -y libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev \
    libcurl4-openssl-dev ffmpeg            # ffmpeg CLI so gera os conteudos
tools/host_player/setup.sh                 # FFmpeg 7.1 do wheel PyAV + headers do devkitPro
tools/host_player/make_fixtures.sh         # conteudos de teste (~600 MB em build/)
tools/host_player/build.sh                 # compila build/host_player/bin/harness
```

`setup.sh` usa os headers de `$DEVKITPRO/portlibs/switch/include` (padrao
`/opt/devkitpro`). O enum `AV_PIX_FMT_NVTEGRA` fica no fim do enum, entao os
headers do port continuam compativeis com as bibliotecas do PyAV.

## Rodar o roteiro

```sh
tools/host_player/suite.sh 200 100 30   # rede boa: latencia 200+-100 ms, 30 Mbps
tools/host_player/suite.sh 500 250 10   # Wi-Fi ruim: 500+-250 ms, 10 Mbps
```

Conteudos gerados no formato real de producao:

| Conteudo | Formato | O que cobre |
|---|---|---|
| `r2/` | HLS igual ao `hls-preparer.js`: fMP4 6 s, `audio_0_jpn`/`audio_1_por`, legenda VTT unica por faixa | abrir, pausar, Continuar, saltos, troca de audio, legendas |
| `anime.mp4` | MP4 progressivo | provedor de anime com Range |
| `api/media/hot/...` | remux de torrent: fMP4 `frag_keyframe` sequencial + sonda + legenda ASS->VTT progressiva | anime via torrent |

Os logs ficam em `build/host_player/run/suite_*.log`. Linhas uteis:
`INJECT` (botao), `VIDEO first frame`/`VIDEO gap` (quadros), `AUDIO pause/play`,
`TRACE ...` (mesmo trace de `sdmc:/switch/.nplay-player-trace.log`) e
`RESULT`/`SUMMARY`.

## Rodar um caso isolado

```sh
cd build/host_player/run
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LD_LIBRARY_PATH=../lib \
SCRIPT_AFTER_FRAME=1 SCRIPT="3000:R,3300:R,9000:Y,10000:DOWN,10500:A,16000:MINUS" \
SNAPDIR=. SNAPS="4,11" AUDIO_PREF=1 \
../bin/harness http://127.0.0.1:8765/r2/index.m3u8 m3u8
```

Variaveis do harness: `SCRIPT` (ms:BOTAO, botoes A B X Y L R ZL ZR PLUS MINUS
LEFT RIGHT UP DOWN), `SCRIPT_AFTER_FRAME` (tempos a partir do 1o quadro),
`START` (retomada), `AUDIO_PREF` (0 dublado, 1 legendado), `HAS_NEXT`,
`EPISODES` (painel Episodios), `SEQUENTIAL`, `HOT_SID`, `DELIVERY`,
`BASE_URL`/`EXTRA_CA` (API HTTPS local), `SNAPDIR`/`SNAPS` (capturas BMP),
`AVDEBUG=48` (log do FFmpeg).
