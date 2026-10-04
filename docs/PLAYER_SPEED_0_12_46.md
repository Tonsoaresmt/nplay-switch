# Abertura e trocas mais rapidas — 0.12.46

Data: 04/10/2026. Continuacao da 0.12.45 (`docs/PLAYER_STABILITY_0_12_45.md`).
Medido com `tools/host_player/` (player_run REAL, FFmpeg 7.1, conteudo no
formato do R2, servidor com latencia/banda). Nao substitui o Switch real.

## Onde estava o tempo

Com a 0.12.45, pausar/retomar ja respondia em ~40 ms. A demora estava na
abertura: em Wi-Fi ruim (500 ms, 10 Mbps), 4,9 s dos ~5,7 s ate o primeiro
quadro eram requisicoes pequenas feitas uma depois da outra pelo FFmpeg
(master, playlist de cada faixa, init de cada faixa). Alem disso:

- todas as faixas de audio eram abertas (playlist + init + 2 segmentos cada),
  inclusive a que nao toca;
- em Legendado, a legenda do master (playlist + VTT) era baixada ANTES do
  primeiro quadro, e trocar de legenda congelava o video durante o download;
- entre episodios encadeados, o app recarregava o catalogo da Home justamente
  na abertura do episodio seguinte, e o JSON ficava na memoria durante ele;
- o progresso salvo era pedido so depois de `/stream`, em outra tela de espera.

## O que mudou

1. **Busca paralela de metadados** (`nplay_curl_avio_hls_prefetch`). Logo apos o
   master, as playlists (variantes primeiro, depois todos os audios) e depois as
   secoes init sao baixadas em ate 4 conexoes para o cache curto. O FFmpeg pede
   na mesma ordem e recebe do cache. A base de resolucao e a URL original e a
   query NAO e herdada (`hls_manifest_resolve_like_ffmpeg`), conferido no host:
   com `?token=` e com 302 o FFmpeg pede exatamente essas URLs.
2. **So o audio escolhido** (`player_hls_choose_audio`, `hls_manifest_keep_audio`).
   A politica de audio roda sobre as renditions do master e o demuxer recebe um
   master com uma unica faixa. O painel continua listando todas (ordem do master
   = ordem antiga das AVStreams, entao pistas de continuidade nao mudam); trocar
   de audio ja reabre a fonte. So filtra com 2+ faixas, todas com URI e no mesmo
   GROUP-ID; caso contrario mantem o comportamento antigo (`audio-all`).
3. **Legenda do master em segundo plano** (`SubtitleFetch`). Abrir nao espera a
   legenda; trocar mantem a faixa atual ate a nova chegar. Cada thread auxiliar
   registra uma flag de cancelamento (`nplay_curl_avio_set_thread_cancel`) e os
   downloads dela sao abortados ao sair. Torrent (progressivo) nao mudou.
4. **Home nao recarrega entre episodios encadeados** (`g_playback_chain`); um
   unico recarregamento quando a sequencia termina.
5. **Progresso pedido junto com `/stream`** (`resolve_progress_thread`), sem a
   segunda tela "Retomando seu video". Falha de `/stream` nao espera o progresso.
6. Rotulos de legenda do master seguem o mesmo padrao das demais (nome tecnico
   vira idioma; numeracao quando ha mais de uma).

## Medicoes (A/B, 3 rodadas, Legendado com legenda PT)

| Cenario | 0.12.45 | 0.12.46 |
|---|---|---|
| 1o quadro, rede boa (200 ms, 30 Mbps) | 2,7–3,0 s | 1,25–1,5 s |
| 1o quadro, Wi-Fi ruim (500 ms, 10 Mbps) | 6,3–7,0 s | 2,7–3,6 s |
| Salto +60 s, rede boa | 0,63–0,87 s | 0,39–0,49 s |
| Salto +60 s, Wi-Fi ruim | 1,5–1,9 s | 1,0–1,4 s |
| Troca de audio, Wi-Fi ruim | 1,7–2,3 s | 1,5–1,8 s |
| Troca de legenda (video parado apos A) | ~1,1 s | ~0,1 s |

Em Wi-Fi ruim o salto passou a ser limitado pela banda: o segmento de 6 s
precisa chegar ate o ponto pedido (preroll). Sair do player enquanto uma
legenda baixa leva ate ~1 s (intervalo do callback do libcurl).

## Validacao local

- ARM64 limpo com `-Werror`; simbolos HLS/HTTPS/NVTEGRA presentes.
- `tools/host_player/suite.sh`: 18/18 em rede boa e em Wi-Fi ruim.
- 18 testes C de host (inclui novos casos em `test_hls_manifest.c`), 8 testes
  Node (sequencia de episodios agora prova a Home adiada) e 153 assercoes do
  `validate_release.ps1` (7 novas).
- Cenarios extras: trocar audio e depois saltar mantem PT; sair durante a
  abertura e durante o download da legenda encerra limpo.

## Pendente no Switch real

1. Abrir filme e episodio R2 (Dublado e Legendado): tempo ate o 1o quadro,
   legenda aparecendo logo depois.
2. Y: trocar audio varias vezes; X: trocar legenda com o video rodando.
3. Maratona de 3+ episodios seguidos e voltar ao menu (Home carrega uma vez).
4. Trace: procurar `meta-prefetch`, `audio-filter`, `subtitle-manifest-applied
   ... async=1`; `audio-filter-mismatch` ou `audio-all` indicam master diferente
   do esperado.
