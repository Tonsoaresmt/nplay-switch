# Letreiros posicionados nas legendas (0.12.48, 04/10/2026)

## Problema
Fansubs de anime (ASS) posicionam placas, onomatopeias e creditos com `\pos`,
`\an` e o alinhamento do estilo. O backend convertia com `ffmpeg -c:s webvtt`,
que descarta tudo isso: no site, na TV e no Switch esses letreiros caiam
embaixo, empilhados com a fala (relato com captura do player web).

## Correcao em tres partes
1. **Backend (`Tonsoaresmt/Nplay`, `src/media/subtitles/ass-webvtt.js`)**:
   o empacotador R2 (`hls-preparer.js`) e a extracao do torrent/TorBox
   (`hot-probe.js`, inclusive em fluxo) extraem ASS (`-c:s ass`) e convertem
   localmente. Fala comum (embaixo-centro, margem baixa) sai sem
   configuracao; letreiro recebe `position:X% line:Y% align:A`. Desenho
   vetorial (`\p`) e `Comment` saem; camadas repetidas viram um cue.
   - Sem alinhamento apos virgula (`line:30%,end`): o Chromium descarta a
     configuracao inteira e nao implementa lineAlign/positionAlign
     (verificado no Chromium 141). A ancora horizontal vem de `align`; a
     vertical e incorporada ao `line` (topo da caixa, 5% por linha).
2. **Web/TV (`public/js/player/subtitle-layout.js`)**: `line` em % com
   `snapToLines=false` = letreiro, desenhado em `#vsigns` dentro do retangulo
   real do video (tarjas). `position` sozinho NAO marca letreiro: o hls.js poe
   position=50 em cues comuns em alguns navegadores.
3. **Switch**: o demuxer WebVTT do FFmpeg entrega as configuracoes como
   `AV_PKT_DATA_WEBVTT_SETTINGS` (o decoder webvtt->ass as perde).
   `subtitle_settings_parse` le essa side data (sem NUL); `SubtitleStoreCue`
   passou a 20 bytes com x/y/ancoras; `subtitle_store_text`/`subtitle_queue_text`
   devolvem so falas e `*_signs` devolvem os letreiros ativos (ate 8, texto
   repetido uma vez). O HUD desenha em `draw_signs` com fonte normal (23 px),
   contorno e sem tarja, preso ao retangulo do video.

## Limites
- Pacotes R2 ja publicados mantem o WebVTT antigo (sem posicao) ate a legenda
  ser refeita. O schema `multitrack-v4-subtitles` NAO foi alterado de proposito
  para nao disparar reprocessamento em massa; reparo deve ser controlado.
- Torrent/TorBox ganham posicao logo apos o deploy do backend.
- Tamanho, cor, rotacao, `\move` animado e fontes do ASS nao sao reproduzidos;
  so a posicao inicial e o alinhamento.

## Validacao
- Backend: `scripts/ass-webvtt-test.mjs` (com ffmpeg real),
  `scripts/subtitle-layout-test.mjs`, debrid/forced/multitrack/media-worker.
- Chromium real: parser nativo de WebVTT + captura com o CSS do site.
- Switch: `test_subtitle_queue`, `test_subtitle_store`, host suite com R2
  (`subsign`) e torrent (`hotretry`) conferindo o letreiro no ponto certo e
  fora da fala. NRO ARM64 `-Werror`. Pendente: conferir no console real.
