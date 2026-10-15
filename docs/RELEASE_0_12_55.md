# Nplay Switch 0.12.55 — candidata local

- Salto e retomada TorBox por reinicio de remux quando a fonte anuncia esse
  suporte. Tempo do filme, progresso e legendas acompanham o trecho escolhido.
- Recuperacao de remux preserva checkpoint/pausa e usa o plano do servidor;
  HTTP nao remonta bytes do inicio dentro da pipeline antiga.
- Erros HTTP permanentes deixam de receber a mesma repeticao dos temporarios.
- Heartbeat considera o tempo real gasto em rede. Pedido renovado e confirmado
  apos apresentar video, com snapshot protegido entre threads.
- Removido fallback HLS antigo que ainda acionava seek interno FFmpeg.
  R2 VOD usa playlists posicionadas; MP4 conserva o salto por Range.

Build ARM64 completo -Werror, recompilacao final e bateria completa local com
fixtures de midia/simbolos passaram. Inclui API C real com rede simulada,
regressoes de retomada/touch/legendas, 100 trocas pthread concorrentes e FFmpeg
CLI8.1.1 com HTTP local: Range, remux chunked, 403 e 503. SDK Switch e7.1;
esses resultados nao substituem teste fisico com VisionQuest e suas fontes.

Fontes hot legadas sem contrato de restart continuam sem busca. Saltos remux
dependem do GOP e da resposta do servidor. Playlist HLS nao posicionavel pode
retornar erro com checkpoint preservado. Extracao hot ainda pode exigir espera.

NRO local: 24.291.151 bytes. SHA-256:
`2d8da20f5c66cfd7b5aec1bc688bce99ef8d119073492b177b1481684ea54096`.

Sem release/push desta candidata. Relatorio:
`docs/PLAYER_DELIVERY_HARDENING_0_12_55.md`.
