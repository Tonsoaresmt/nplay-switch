# Estabilidade de rede do NRO — 0.12.41

## Evidência e escopo

O relato é de carregamentos/reconexões repetidos durante reprodução longa.
Os arquivos fornecidos em `C:/Users/Everton/AppData/Local/Temp/` foram lidos:
`nplay-player-trace.log`, `nplay-player-trace.prev.log`,
`nplay-network-trace.log` e `nplay-player-boot.txt`.
O trace longo pertence à **0.12.38**, não à versão corrigida.

- Vídeo HLS R2 iniciado e reproduzido até aproximadamente 3042 segundos.
- Segmentos com primeiro byte em 21013/21700/22572 ms; duas falhas curl 28
  em aproximadamente 30000 ms. Depois disso houve novas respostas HTTP 200.
- Posição ficou em 3072 segundos apesar dos segmentos seguintes chegarem.
- Demux: 128 pacotes, pico 3608 KiB, reserva de vídeo máxima de 2 segundos,
  terminal 0. Sem falha de decoder de áudio, sem frames descartados.
- Heartbeat/progresso tiveram HTTP 200 e algumas interrupções por timeout.
  Não foram observados 401/403 ou falhas de certificado nesse incidente.

Isso demonstra atrasos de transporte e falta de retomada no caso registrado.
Não demonstra que toda reconexão tem a mesma causa, nem elimina a possibilidade
de expiração em outras sessões. A política de expiração da 0.12.40 permanece.

## Correção

1. O callback de leitura de segmentos HLS entregava `EAGAIN` depois de apenas
   300 ms sem dados. No FFmpeg 7.1, `fill_buffer` marca `eof_reached` e `error`
   para retornos negativos. Um parser de fMP4/HLS pode permanecer preso depois
   dessa interrupção no meio de uma estrutura. A leitura agora aguarda no worker
   demux até dados, EOF, erro definitivo ou cancelamento: não injeta esse erro
   temporário no parser. A UI roda separadamente; cancelamento é conferido a
   cada espera de 100 ms. Não foi criado um bloqueio de rede na thread de desenho.
2. Transferência HLS que passa oito segundos sem receber nenhum byte de corpo
   abandona a tentativa. A próxima tentativa usa conexão nova, preservando o
   reaproveitamento TLS/TCP nas transferências normais.
3. O limite de oito segundos não se aplica a corpo já recebido/backpressure.
   Bytes parciais continuam preservados e retomados por offset; validação de Range,
   TLS, limite de memória e erros HTTP definitivos permanecem inalterados.
4. O supervisor existente escala stalls sem quadro/áudio depois de 12 segundos
   para recuperação controlada. Não foi ampliada a fila arbitrariamente: o trace
   já chegou perto do teto de 4 MiB, portanto mais slots não resolveriam isso.

Fontes oficiais consultadas: [FFmpeg 7.1, aviobuf.c](https://ffmpeg.org/doxygen/7.1/aviobuf_8c_source.html)
e [libcurl, LOW_SPEED_TIME](https://curl.se/libcurl/c/CURLOPT_LOW_SPEED_TIME.html).
O código local do FFmpeg também foi conferido em `.build-thirdparty`.

## Validação local

- `tools/test_curl_avio_wait.mjs --baseline` executa o callback C original do commit
  `48a75f9` (0.12.40): falha na chegada de dados após 1500 ms. Reprodução determinística,
  não simulação de uma API reimplementada.
- Sem `--baseline`, o mesmo callback extraído da fonte corrigida passa: gaps
  consecutivos, bytes já em buffer, ring circular, cancelamento, EOF/erro,
  prazo de primeiro byte e ausência de abort indevido com corpo já recebido.
- Harness também executa o callback real de progresso libcurl: limite 7999/8000 ms,
  transferência parcial de 30 s, fechamento, seek e caminho não streaming.
- Build ARM64 com `-Wall -Wextra -Werror` passou.
- `tools/validate_release.ps1 -SkipBuild` completo passou, **sem** pular fixtures:
  worker pthread real, buffer/memória, UI assíncrona, políticas de áudio/recuperação,
  touch, HLS multifaixa início+seek, WebVTT, remux chunked e símbolos ARM64.
- NRO: 24201039 bytes; SHA-256
  `afea68abe5dd91d8734eb3a474cff0c812ef05b88e64b57c647fd2cfa0c2fc5b`.

## Limites e próxima verificação

O harness de espera usa SDL simulado; não mede Wi-Fi, CDN ou decoder no console.
Fixtures reais usam FFmpeg no computador, não a GPU do Switch. Não declarar
reprodução física comprovada nem prometer ausência de buffering com rede ruim.

No Switch: confirmar versão 0.12.41, assistir ao mesmo filme por mais de 60 min,
verificar que passa do ponto anteriormente travado; testar pausa/retomada, seek,
troca de áudio e queda de Wi-Fi curta/longa. B/touch devem permitir sair durante
espera/recuperação. Coletar os traces novamente se falhar, com título, posição,
tipo de fonte e duração da interrupção. Não expor URLs assinadas ou identificadores
secretos. Não realizar reprocessamento em lote de mídia para testar transporte.

Continuidade: checkout `C:/NplaySwitch/.codex-tmp/switch-access-expiry`, branch
`codex/switch-access-expiry`; publicação do código em `codex/switch-rebuild`.
Arquivos alterados: `source/curl_avio.c`, harness, validador, versão e documentação.
Legendagem remux/idioma não foram modificados nesta rodada.
