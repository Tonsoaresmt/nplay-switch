# Nplay Switch 0.12.26

Release de consolidacao do player nativo SDL2/FFmpeg/libcurl. Esta rodada ataca
os travamentos relatados ao pausar/retomar, buscar, trocar audio e ativar
legendas, sem substituir a arquitetura do NRO.

## Causas confirmadas e correcoes

- A 0.12.25 evitava a corrida entre `av_read_frame` e os controles reabrindo
  toda a fonte para cada seek ou troca de faixa. Era seguro, mas caro: repetia
  manifesto, probe, decoders e primeiro buffer, parecendo que o player travava.
- O worker de demux agora possui uma barreira cooperativa. Seek e troca aguardam
  a leitura corrente terminar, congelam o worker, descartam pacotes da geracao
  antiga e somente entao chamam `avformat_seek_file`/flush dos decoders.
- Seek de 10/60 s, timeline e toque permanecem na mesma sessao quando a operacao
  e aceita. Trocar audio ou legenda abre o novo decoder de forma transacional e
  preserva o antigo ate o reposicionamento ser confirmado.
- Cancelar antes da barreira nao altera a reproducao. Cancelamento, timeout ou
  falha depois que o demuxer foi tocado nunca continua num contexto parcial: o
  supervisor reabre a fonte na posicao segura. Se a operacao interna nao entregar
  novo quadro em 20 s, a mesma recuperacao e acionada automaticamente.
- O callback de interrupcao do FFmpeg deixou de ler campos nao atomicos a partir
  da thread de demux. A parada entre threads usa somente `demux_abort` atomico.
- Audio SDL permanece pausado durante o preroll e so e liberado quando o primeiro
  quadro da nova geracao esta pronto. A fila e limpa depois de um seek confirmado;
  falha de `SDL_QueueAudio` agora encerra com diagnostico, em vez de congelar.

## PT-BR e legendas

- Em manifests HLS reais, o `NAME="Portugues (Brasil)"` pode chegar pelo metadata
  `comment`, nao por `title`. O cliente agora consulta `title`, `comment` e `name`
  antes da politica de idioma. Assim PT-BR vence um audio ingles marcado DEFAULT
  quando o perfil esta em Dublado.
- `pkt_timebase` e definido para decoders de audio e legenda. Cues WebVTT usam
  primeiro `AVSubtitle.pts` em `AV_TIME_BASE`, depois PTS/duracao do pacote.
- A fila de cues subiu de 8 para 32. Isso evita perder as primeiras falas quando
  uma playlist entrega varios cues de um segmento antes de eles chegarem a tela.
- O painel continua paginado e mostra nome, idioma, codec/canais e faixa atual.
  A tela de preparacao conserva a animacao de pipoca embutida no NRO.

## Fluxos preparados

- URLs preparadas vindas de Historico/Biblioteca voltaram a propagar
  `container=m3u8` e `DELIVERY_R2`. Sem isso, o mesmo HLS podia cair no caminho de
  arquivo simples e perder o tratamento fMP4/AAC/WebVTT usado por filmes/series.

## Validacao local reproduzivel

- Build ARM64 completo com `-Wall -Wextra -Werror`.
- `tools/validate_release.ps1`: contratos site/Switch, API, episodios, relogio,
  politica de audio, parser HLS, fila de legenda e simbolos do NRO.
- `tools/test_hls_player_fixture.mjs`: gera HLS fMP4 de 12 s com video, ingles
  DEFAULT, PT-BR alternativo e duas legendas WebVTT. Confirma cinco streams,
  metadata `comment`, time bases, cues, abertura, seeks para frente/tras e as duas
  renditions de audio.
- `tools/test_chunked_remux.mjs`: 203 quadros reconhecidos sem Range.
- Artefato final: `Nplay.nro`, 24.155.983 bytes.
- SHA-256: `42601cc290c2f145a555d24d169f5fbf395ffce216fed61b0d5dfd48f8f844a7`.

## Limite da comprovacao

Os testes exercitam as politicas, manifests, decodificacao e artefato ARM64, mas
nao executam o NRO numa GPU/OS Horizon real. Antes de considerar a estabilidade
encerrada, instalar esta versao e testar no Switch: filme, serie, anime e dorama;
pausa de 30/120 s; L/R/ZL/ZR; timeline confirmar/cancelar; todas as faixas de
audio; legenda PT-BR desligar/ligar; e queda de Wi-Fi curta. Em falha, fotografar
Configuracoes > X Diagnostico imediatamente apos reabrir.
