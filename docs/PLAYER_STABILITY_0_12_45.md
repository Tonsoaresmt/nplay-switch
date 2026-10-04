# Estabilidade do player — 0.12.45

Data: 04/10/2026. Relato do usuario na 0.12.44: player "emperrado", recarregando
a cada avanco/pausa, troca de idioma lenta, animes instaveis, legendas falhando e
sem como trocar de episodio dentro do player como no site/TV.

## Metodo

`tools/host_player/` roda o `player_run` **real** no Linux com o mesmo FFmpeg 7.1
do Switch (libavformat 61.7.100, do wheel PyAV) contra conteudos gerados no
formato real de producao e um servidor com latencia/banda configuraveis. Botoes
sao injetados por roteiro; quadros, pausas de audio, trace e rede ficam medidos.
O roteiro `suite.sh` tem 18 verificacoes e roda em rede boa (200 ms, 30 Mbps) e
Wi-Fi ruim (500 ms, 10 Mbps). A 0.12.44 original falha em 10 delas.

## Causas encontradas e correcoes

### 1. Seek, Continuar e troca de audio no HLS do R2 (causa principal)

O seek interno do FFmpeg 7.1 em HLS fMP4 com audio em rendition separada (o
formato do `hls-preparer.js`) **nao reinicia o demuxer mov de cada rendition**:
depois do seek ele continua emitindo amostras do indice antigo (`sample 1, 2,
3...` com tempos de 0 s) enquanto os bytes ja vem do segmento novo. Resultado,
reproduzido no player, em PyAV e no `ffmpeg -ss` CLI:

- o filtro de tempo do HLS nunca alcanca o alvo e **descarta o audio inteiro ate
  o fim do episodio** (todos os segmentos baixados e jogados fora);
- o video chega desalinhado (`Invalid NAL unit size`) e nenhum quadro aparece;
- o watchdog de 12/20 s escala para a reabertura, que tambem usa seek: o usuario
  ve "Aguardando dados"/"Preparando" repetidos.

Correcao (sem mexer no FFmpeg): **abertura posicionada**. Seek, Continuar e troca
de audio reabrem a fonte com cada playlist de midia ja comecando no segmento do
ponto pedido (`hls_media_playlist_trim`, testado em `test_hls_manifest.c`). O
FFmpeg abre limpo e os tempos continuam absolutos (tfdt). Preroll descarta o que
vem antes do alvo. Master, playlists e secoes `init-*.mp4` ficam num cache curto
(10 min, 2 MB, limpo ao sair do player), entao cada salto so baixa segmentos.

| Cenario (rede boa) | 0.12.44 | 0.12.45 |
|---|---|---|
| Continuar em 66 s | nenhum quadro | 1o quadro em ~2,7 s (igual a comecar do zero) |
| Salto +30/+60/-10 s | nenhum quadro | video volta em ~1,5-1,9 s |
| Troca de audio | falha | ~1,4-1,9 s |
| Salto em Wi-Fi ruim | nenhum quadro | ~1,6-2,8 s |

### 2. L/R/ZL/ZR congelavam a imagem esperando A

Desde a 0.12.31 o primeiro toque pausava o video e abria uma previa que so saia
com A. Agora funcionam como as setas do site/TV: o video continua tocando, os
toques somam (+10/+60 s) com aviso "+30 s -> 1:23", e o salto e aplicado 700 ms
depois do ultimo toque. B cancela o salto pendente. A previa pelo analogico foi
mantida. Durante a reabertura o ultimo quadro fica na tela escurecido com
"Indo para 1:23" (antes: tela cheia de preparacao, a sensacao de "recarregando").

### 3. Anime via torrent (remux sequencial)

- **Duracao errada marcava o episodio como visto.** O fMP4 gerado na hora expoe
  so o primeiro fragmento (~2 s). O progresso saia como `pos=30 dur=2` e o backend
  marca `completed` a partir de 92%: o episodio sumia de Continuar e a serie
  pulava para o seguinte. Agora usa `duration` da sonda do servidor (ja baixada
  para as legendas); sem ela, envia 0 (o backend guarda o ponto sem concluir).
- **Audio engasgando.** O servidor remuxa com `frag_keyframe`: em anime (GOP de
  5-10 s) o arquivo traz blocos so de video e depois o audio do trecho. O player
  processava em ordem e o audio acabava (18 faltas em 40 s no teste). A reserva
  de leitura passou a 512 pacotes (12 MB para remux sequencial) e, com menos de
  1 s de audio na fila do SDL, o proximo pacote de audio ja recebido e
  decodificado antes do video (`demux_worker_take_stream`). Resultado: 0 faltas.

### 4. Legendas

- R2: continuam carregando pelo master depois de cada salto/reabertura.
- Duas falas simultaneas (dialogo + placa, comum em anime) somam 3-4 linhas; o
  limite de 2 linhas cortava a segunda. Agora ate 4 linhas.
- Torrent: legenda progressiva confirmada no teste (falas no tempo certo).

### 5. Trocar de episodio dentro do player

Novo painel **Episodios** (direcional esquerdo, dica no topo do HUD): lista as
temporadas carregadas, marca o atual e os vistos; cima/baixo escolhe, L/R pagina,
A toca qualquer episodio (inclusive o anterior), B fecha. O episodio escolhido
volta por `PlayerResult.chosen_item_id` e `play_episode_sequence` o toca direto.

### 6. Nomes das faixas

O empacotador publica `NAME="audio_0"`. O painel mostrava esse nome cru; agora
mostra o idioma (Japones, Portugues...).

## Validacao local

- Build ARM64 com `-Wall -Wextra -Werror`, sem avisos.
- 18 testes C de host e testes Node do validador (demux worker, supervisor,
  seek barrier, sequencia de episodios, subtitle IO, curl AVIO, remux, backpressure)
  passaram; os de extracao foram atualizados para os novos caminhos.
- `test_hls_player_fixture.mjs` depende de um ffprobe que liste legendas HLS
  (7.x); com o ffmpeg 6.1 do Ubuntu ele falha igual na 0.12.44 — ambiente.
- 146 assercoes de texto do `validate_release.ps1` (11 novas) passaram.
- `tools/host_player/suite.sh`: 18/18 em rede boa e em Wi-Fi ruim.

## Pendente no Switch real

1. Filme e episodio R2: L/R/ZL/ZR varias vezes, Continuar no meio, troca de
   audio; observar o tempo ate o video voltar e se a imagem fica na tela.
2. Anime via torrent: 10 min seguidos (audio continuo), legendas com placa,
   sair no meio e conferir que o episodio continua em "Continuar assistindo".
3. Painel Episodios (esquerda) em serie com 2+ temporadas, escolher o anterior.
4. Se algo falhar, enviar foto do Diagnostico e o trace `.nplay-player-trace.log`;
   procurar `positioned-open`, `playlist-positioned`, `quick-commit`,
   `sequential-duration`, `audioAhead` e `episode-chosen`.

Limites conhecidos: NVTEGRA, driver de audio e Wi-Fi do console nao rodam no
host. A abertura inicial em Wi-Fi ruim continua ~6 s (pedidos sequenciais do
FFmpeg); pular abertura (AniSkip) e seletor de qualidade do site nao foram feitos.
