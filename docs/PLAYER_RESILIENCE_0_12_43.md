# Resistência a pausas, rede e busca — NRO 0.12.43

## Contexto e limites da investigação

Usuário relata muitas recuperações após controles e sensação de player frágil.
A última versão confirmada pelo usuário é 0.12.41; a 0.12.42 corrigiu o retorno
automático ao começo. Nenhum trace novo foi fornecido nesta rodada. Não afirmar
que os mecanismos abaixo explicam todas as interrupções no aparelho.

Mantida arquitetura SDL2/FFmpeg/libcurl, mesma fila de 128 pacotes / 4 MiB,
mesmo ring de 4 MiB por segmento, TLS verificado e checkpoint da 0.12.42.

## Pontos corrigidos

### 1. Pausa local não é lentidão da rede

`wr_ring` espera quando o consumidor não libera espaço. Pausa, menus e timeline
podem manter o produtor nesse estado por muito tempo. O `LOW_SPEED_TIME=30`
avaliava velocidade da transferência incluindo espera do consumidor.

Para segmentos streaming, esse detector foi substituído por oito segundos sem
bytes reais de corpo, via `xfer_cb`. O relógio é rearmado a cada escrita depois
de conseguir espaço no ring, excluindo a espera local. Primeiro byte continua
com prazo de oito segundos. Metadados/arquivos mantêm a configuração anterior.
Não desabilitar detecção de conexão parada: o novo callback aborta idle real,
mantendo retomada por offset e recuperação controlada como último recurso.

Prova com libcurl real de host (8.13.0), localhost: callback de escrita segura
consumo por quatro segundos; resposta em partes aos 0, 1 e 4.2 s. Prazo antigo
escalado para 2 s: curl 28, só 512 bytes. Detector atual: curl 0, todos os 1024
bytes. Outro endpoint envia 128 bytes e para: callback atual aborta com curl 42.
Essa prova reproduz interação entre backpressure e espera curta após liberação,
não a rede do usuário. Um teste de resposta já completa não causou timeout;
portanto não generalizar que toda pausa dispara o erro.

Host usa libcurl 8.13.0; SDK do Switch usa 7.69.1. Sem prova física, efeito no
console ainda é hipótese respaldada pela estrutura do código e pela reprodução
de host. Callback C real de `wr_ring` também foi executado com espera SDL simulada
de 60 s, verificando rearme do prazo e cancelamento/EOF/bytes preservados.

Fontes oficiais: [LOW_SPEED_TIME](https://curl.se/libcurl/c/CURLOPT_LOW_SPEED_TIME.html)
e [XFERINFOFUNCTION](https://curl.se/libcurl/c/CURLOPT_XFERINFOFUNCTION.html).

### 2. Leitura ocupada não exige destruir o player

Ao confirmar seek, o worker deve atingir uma barreira antes de tocar em FFmpeg.
O prazo de 5 s podia terminar sem nenhum contexto modificado. Antes, o resultado
2 era tratado como falha de seek, causando reabertura completa.

Agora o helper distingue **ocupado** (5) de erro. Na timeline conserva destino
e prévia, A tenta novamente/B cancela. No touch conserva reprodução e informa
que deve tentar novamente. Se o seek realmente começou e falhou/cancelou, mantém
a reabertura segura existente: contexto pode estar modificado. Não interromper
av_read_frame artificialmente, não limpar erros privados do HLS, não fazer seek
concorrente. Alterações de faixa mantêm suas regras transacionais existentes.

`test_seek_barrier.mjs --baseline` usa helper C de d9c2df6: falha no caso ocupado.
Atual passa: espera ocupada/cancelamento sem clear/seek, worker terminado,
barreira atingida, operação interrompida e seek recusado. SDL/FFmpeg são stubs.

### 3. Pequena reserva antes de sair de buffering

Após starvation visível >=250 ms, o player procura juntar 0.75 s de timestamps
de vídeo antes de consumir a fila novamente. Não aplica a abertura, preroll nem
troca de faixa. Isso procura reduzir retomadas de um instante que imediatamente
voltam a carregar; é uma melhoria de política, não ganho medido no aparelho.

Espera extra termina aos 3 s, em EOF/erro, fila cheia ou 75% do teto de bytes.
Assim não bloqueia fila com pacotes grandes/PTS desconhecidos. Snapshot acontece
com mutex; não aumenta memória. Controles continuam no loop principal. Worker
pthread real passou testes de snapshot, clear/geração, barreiras e leaks; política
pura cobre prazos, reserva suficiente, slots, bytes e terminal.

Tradeoff: pode esperar um pouco mais em uma recuperação individual para reduzir
oscilações sucessivas. Confirmar esse equilíbrio no hardware antes de alterar
limites. O watchdog de recuperação da sessão continua limitado; não esconder
falhas reais nem ficar esperando indefinidamente.

## Telemetria e validação

- Eventos HTTP do segmento agora incluem `idle` e `bytes`, sem URL/sid secreto.
- `buffering-end` inclui tamanho/tempo da reserva após retirada do pacote.
- Build ARM64 -Wall -Wextra -Werror passou.
- validate_release.ps1 completo passou sem SkipMediaFixtures: libcurl real,
  callbacks de transporte, supervisor, barreira, pthread, memória, políticas,
  áudio, touch, HLS multifaixa/seek, VTT, remux e símbolos de hardware.
- NRO 24201039 bytes; SHA-256
  `3b3871db55107c100c54d2b66bbedf64518089b869812b26ba251ccb875acfeb`.

Probe de host requer HOST_CC, HOST_CURL_HEADERS e HOST_CURL_LIBRARY (defaults
devkitPro/MSYS deste Windows). A libcurl usada em runtime não é a do Switch.

## Continuidade / teste obrigatório no console

Checkout C:/NplaySwitch/.codex-tmp/switch-access-expiry; branch local
codex/switch-access-expiry, publicação codex/switch-rebuild. Arquivos principais:
curl_avio.c (idle/backpressure), player.c (snapshot/barreira/refill), player_buffer.h
(política) e novos probes em tools. Não mexeu no backend/legendas/idioma.

Confirmar versão 0.12.43. Mesma obra >60 min, pausa 1/5 min, abrir/fechar faixas,
gatilho acidental com B cancela, seek confirmado frente/trás e touch; Wi-Fi fora
por 5/20 s e saída B durante recuperação. Conferir posição e novos traces.
Falta toda essa validação física. Não prometer ausência de buffering: fonte lenta,
CDN, queda de Wi-Fi ou erro de mídia ainda podem exigir recuperar.
