# Player: TorBox, R2 e arquivos diretos — candidato local 0.12.55

15/10/2026. Base 5ff7570; checkout switch-access-expiry, branch hardening.
Relato: VisionQuest reconectando; outro conteudo funcionou. Episodio, fonte,
versao instalada e trace dessa tentativa nao foram fornecidos. Isso nao permite
atribuir a falha observada ao Wi-Fi, TorBox, R2 ou decoder de forma conclusiva.

## Mudancas implementadas

1. **Salto TorBox por reinicio do remux.** O servidor ja anuncia
   `seek_mode=restart` para MKV/AVI convertidos em fMP4. O NRO tratava todo esse
   caminho como impossivel de buscar. Agora conserva a capacidade distinta
   de byte Range e aceita apenas delivery debrid, SID validado, container MP4
   e URL HTTPS exatamente no endpoint de video daquela sessao.

   A consulta cancelavel `/stream/hot/:sid/seek?t=...` roda no worker existente,
   limitada a 16 KiB/8 s. O plano precisa confirmar modo, tempo finito plausivel
   e duracao; erro nao vira inicio zero. O video recebe o mesmo `t` pedido ao
   servidor e conserva os demais parametros, inclusive autenticacao assinada.
   SID/URL nao aparecem nos novos eventos de diagnostico.

   A linha do tempo integra a base do quadro-chave e o preroll descarta somente
   o trecho anterior ao alvo. Retomada, L/R/ZL/ZR, timeline confirmada e audio
   usam reabertura, sem buscar bytes dentro do remux nem chamar seek HLS interno.
   O debounce de 700 ms e a confirmacao do analogico/touch continuam existindo.

   WebVTT progressivo recebe `from=base` no download e a leitura dos cues desconta
   essa base. A URL de identidade da faixa continua sem esse parametro de
   tentativa, conservando a escolha durante saltos. A faixa anterior permanece
   aplicada ate a nova ser aceita. R2 continua usando o corte HLS preexistente.

2. **Recuperacao de fonte com restart.** Renovacao TorBox admite o contrato de
   restart validado, alem de MP4 Range. HOME e falha de rede preservam checkpoint
   e pausa; a proxima tentativa pede o trecho ao servidor. Fontes antigas sem
   esse contrato continuam restritas. Nao foi liberada busca falsa em torrents
   hot que nao possuem a rota de plano debrid.

3. **Politica HTTP.** O FFmpeg nao tenta remontar um fluxo sequencial no byte
   zero dentro da mesma pipeline. O supervisor faz a recuperacao posicionada.
   Repeticao de HTTP foi restringida a 408/429/500/502/503/504. Negativas
   permanentes, como 403/404, retornam ao fluxo de erro/renovacao em vez de
   repetir como qualquer 4xx. TLS e limites de memoria nao foram relaxados.
   O watchdog de starvation tambem atende arquivos com callback de recuperacao,
   alem do HLS. Ele exige quadro ja apresentado, audio vazio e ausencia de
   troca de faixa; pausas/modais nao entram nesse caminho.

4. **Heartbeat e progresso.** Os intervalos agora usam tempo decorrido real,
   incluindo o tempo dentro dos callbacks HTTP. Antes somavam apenas 100 ms
   por volta do loop. Com progresso artificialmente lento por 10 s, o codigo
   da .54 falha na verificacao de cadencia; o atual passa sem rajada de catch-up.
   Isso demonstra o defeito de agendamento, nao prova que encerrou a sessao
   especifica do usuario.

5. **Confirmacao apos renovar.** O pedido hot/TorBox renovado substitui o
   snapshot do worker sob lock curto. O worker copia o descriptor e reinicia
   o contador por geracao, somente depois do primeiro quadro da tentativa nova.
   Callback antigo continua com sua copia; nenhum HTTP ocorre sob o lock.
   Sem isso, o worker continuava confirmando apenas o pedido inicial.

6. **Diagnostico.** Source/resolved registra capacidades sequential/restart,
   sem SID ou URL. A primeira espera normal EAGAIN nao e mais registrada como
   falha de primeira leitura. Plano de salto registra somente alvo/base e
   uma rota redigida para distinguir erro de plano e de decodificacao.

7. **Fallback HLS antigo.** A abertura ainda chamava o seek interno quando
   uma playlist nao podia ser cortada para um alvo distante. Esse caminho foi
   removido, e o helper de seek recusa HLS mesmo se outro chamador tentar.
   Sem playlist posicionavel, conserva o checkpoint e retorna ao supervisor;
   nao libera um salto que possa corromper o estado fMP4. Isto tambem limita
   HLS EVENT/formatos de playlist nao suportados, em vez de simular sucesso.

## Evidencias locais e limites

- `test_playback_restart`: API C real, URLs/plano reais e HTTP simulado.
  Cobre parametros assinados preservados, Range/restart separados, zero
  explicito, resposta malformada, 401/403/404/410/503, timeout/TLS, cancelamento
  tardio, saida anterior conservada e SID/URL/tamanho/NaN invalidos.
- `test_player_supervisor`: supervisor C real com pipeline simulada, incluindo
  retomada restart pausada, volta a um ponto anterior e fonte sequencial legada.
- `test_playback_started`: worker C real com tempo/rede simulados, callback de
  progresso lento e 100 trocas com produtor pthread real durante callback
  antigo, copia imutavel e nenhuma rede sob lock. Baseline .54
  reproduz a falha de cadencia (`--baseline-054`, falha ESPERADA).
- `test_subtitle_usage`: leitura C real dos cues progressivos com bases 598 e
  4 segundos, sem deslocar legendas do R2. Mutex desse teste e simulado.
- `test_player_delivery_fixture`: FFmpeg CLI **8.1.1** real, servidor HTTP
  localhost e arquivos sinteticos: MP4 com Range nao zero, 403 com uma unica
  requisicao, 503 seguido de sucesso e remux chunked reaberto para frente/tras
  com quadros efetivamente decodificados. Os delays de retry do CLI sao curtos.
  Isso nao e player SDL integral nem FFmpeg 7.1 do SDK executando no console.
- `test_hls_seek_guard`: helper C real com API FFmpeg simulada; HLS faz zero
  chamadas a seek interno, MP4 conserva busca nativa e fallback limitado.

O harness Linux integrado nao foi executado: WSL nao esta instalado e nao ha
build/host_player neste ambiente. Nenhuma instalacao de sistema foi feita.
Servidor/API de comparacao: snapshot local c9d0f77 previamente autorizado;
nenhuma conta real, fonte VisionQuest, TorBox pago ou R2 de producao foi usado.
Nao houve escrita/deploy/reprocessamento no backend.

Build ARM64 completo -Werror e recompilacao incremental final passaram.
`validate_release.ps1 -SkipBuild` completo, SEM SkipMediaFixtures, passou no
artefato FINAL com o guard HLS, fixtures reais e simbolos obrigatorios.
O teste adicional de 100 publicacoes pthread tambem passou; nao houve alteracao
do app nem rebuild depois da verificacao final.

NRO FINAL local: 24.291.151 bytes, SHA-256
`2d8da20f5c66cfd7b5aec1bc688bce99ef8d119073492b177b1481684ea54096`.
O sidecar acompanha esse digest. O intermediario b887640... foi substituido.

Esta candidata .55 permanece local; a autorizacao de publicacao anterior era
da .54 e foi cumprida. Nao foi criada release .55 nesta rodada.

## Validacao necessaria no Switch

Anotar T/E e gerar traces de VisionQuest na .55; o modo precisa aparecer em
source/resolved. Comparar R2, TorBox MP4 Range e TorBox restart. Em cada um:
pausa/retomada, saltos agrupados, ponto proximo ao meio, retorno para tras,
audio, legendas, HOME, sono e pelo menos 60 minutos. Conferir posicao absoluta,
sincronia e confirmacao de pedido renovado. Falha deve preservar checkpoint e
permitir B/touch, sem retornar automaticamente ao inicio.

Limites abertos: carregamento hot ainda modal, lista de legendas ate16,
extracao progressiva sem marcador final de completude, falta de crash dump
nativo anterior, fila PCM sem teto novo e fontes sequenciais sem restart.
Novos saltos por remux dependem do tempo de resposta e do GOP da fonte; nao
prometer seek instantaneo, fim de todo buffering ou homologacao fisica.

Referencias primarias consultadas:
- https://ffmpeg.org/doxygen/7.1/http_8c_source.html — read/reconnect em streams.
- https://ffmpeg.org/ffmpeg-protocols.html — HTTP/seek/reconnect.
