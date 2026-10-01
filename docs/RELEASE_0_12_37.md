# 0.12.37 — estabilidade e limites da verificacao (01/10/2026)

## Correcoes implementadas

- Fila demux: 128 slots (antes 32), teto estrito de 4 MiB. Um pacote pendente
  separado pode ocupar ate outros 4 MiB; isso nao inclui buffers internos FFmpeg.
  Barreira de pausa reconhecida mesmo com fila cheia; clear invalida o pacote
  pendente de outra geracao. Pacote individual acima do teto retorna erro controlado.
- Trace worker-summary registra maxPackets, bytes, segundos de video, esperas por
  espaco e leituras vazias. A melhoria de fluidez ainda precisa ser medida no console.
- 15 mutacoes da UI e duas consultas de retomada usam worker com tela responsiva,
  pipoca, cancelamento por B/toque e join antes de liberar parametros emprestados.
  Nao repete automaticamente mutacoes. Cancelamento nao desfaz operacao ja aceita.
- Preferencias atrasadas nao sobrescrevem a edicao nova; consulta apos gravacao
  reconcilia o estado real. Favoritos e exclusoes nao anunciam falso sucesso.
- libcurl deixa de compartilhar conexoes entre threads. DNS/TLS session cache
  permanecem protegidos por locks; todos os handles usam NOSIGNAL.
- Fallback de legendas HLS tambem cobre descoberta parcial do FFmpeg.
- Validacao aceita HOST_CC, TARGET_NM e NPLAY_BACKEND_ROOT. PowerShell 7 necessario
  fora do Windows; Linux nao foi executado. Fixtures ausentes agora causam falha,
  salvo pedido explicito de validacao parcial com SkipMediaFixtures.

## Evidencia executada

- Build ARM64 limpo com -Werror e suite completa tools/validate_release.ps1: PASSOU.
- 100.000 operacoes de politica de buffer, teste do worker real extraido de player.c
  com pthreads: fila cheia por bytes e slots, 25 seeks, barreiras, pacote antigo,
  encerramento, pacote grande e contador de pacotes vivos zerado: PASSOU.
- UI worker simulado: desenho enquanto aguarda, B/toque/quit, sucesso concorrente
  com cancelamento, join e falha ao criar thread: PASSOU.
- Audio, clock, sync, touch, HLS, legendas, API, episodios, pairing, remux real e
  fixture FFmpeg com cinco faixas/duas cues/inicio+seek: PASSOU.
- NRO: 24.188.751 bytes. SHA-256:
  03a56220a682de8a900ca9e0fee6201d518544c179bb579dbea67f91cf9d6354.

## Legendas: nao declarar resolucao universal

Auditoria read-only `node tools/probe_r2_packages.mjs C:/iptv --subtitles-only`
selecionou seis fontes do inventario local e verificou o delivery real:

| Item | Resposta | Renditions de legenda |
| --- | --- | --- |
| 191216 | 200 | 0 |
| 137922 | 200 | 0 |
| 441 | 200 | 0 |
| 233391 | 404 | manifesto indisponivel |
| 233390 | 200 | 0 |
| 177810 | 200 | 8; tres playlists verificadas responderam 200 |

Ausencia no master nao prova que o arquivo original tinha legendas. O inventario
local pode estar atrasado; o 404 precisa ser reconciliado com o banco de producao
antes de remover/alterar fontes. Nenhum pacote nem banco de producao foi alterado.

PC consulta `/api/stream/hot/:sessionId/probe` e extrai VTT por rota adicional.
NRO ainda nao implementa esse caminho de remux. Para HLS R2 existe fallback por
manifesto, mas isso nao recupera legendas ausentes do pacote. O servidor recebe
multitrack-v4-subtitles em mudanca separada; pacotes antigos nao mudam so por deploy.

Proximos passos: comparar mesma fonte/session no PC e NRO (nao so mesmo titulo),
exercitar item 177810 no console e coletar trace de renditions/fallback; implementar
descoberta/extracao hot com worker cancelavel e limites de memoria/tempo. Reparar
pacotes individualmente somente apos confirmar legenda na origem e fonte disponivel.

## Pendencias e continuidade

- Nao houve teste no Switch fisico: nao afirmar fluidez/ausencia de crash validada.
- SDL audio queue continua instrumentada, sem teto novo. Backpressure precisa
  permitir seek/pause/stop e nao descartar PCM arbitrariamente.
- Nplay.nro passa a ser artefato de Release, nao de source control. Historico Git
  preservado; nao houve reescrita para remover binarios antigos.
- Repositorio de trabalho: .codex-tmp/switch-0.12.18; backend separado em
  .codex-tmp/backend-subtitles-v4. Nao editar a copia raiz antiga para continuar.
- Antes de publicar, conferir novamente digest e referencias remotas. A validacao
  local nao constitui prova de deploy nem de atualizacao do console.

Referencias tecnicas: https://curl.se/libcurl/c/CURLSHOPT_SHARE.html,
https://curl.se/libcurl/c/threadsafe.html,
https://wiki.libsdl.org/SDL2/SDL_QueueAudio.

## Publicacao confirmada

Commit 91da091 enviado por fast-forward atomico a main e codex/switch-rebuild.
Release publica/latest v0.12.37: API GitHub confirmou Nplay.nro com 24.188.751
bytes e digest igual ao build acima. O arquivo nao foi apagado do disco: somente
deixou de ser rastreado em Git. Binarios historicos continuam recuperaveis.
Backend funcional 5b6b534 (documentacao de rollout cd8848e) enviado a main do Nplay;
verificar docs/SWITCH_SUBTITLE_V4_2026_10_01.md naquele repo para o deploy.
