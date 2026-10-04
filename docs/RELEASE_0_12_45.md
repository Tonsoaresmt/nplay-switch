# Nplay Switch 0.12.45

## Player mais estavel e parecido com o do site

- **Avancar/voltar funciona no HLS do R2.** O seek do FFmpeg 7.1 corrompia as
  faixas fMP4 (audio descartado ate o fim, nenhum quadro, "recarregando"). Seek,
  Continuar e troca de audio agora reabrem ja no segmento certo.
- **L/R/ZL/ZR sem congelar.** O video continua tocando, os toques somam e o salto
  e aplicado sozinho; B cancela. A imagem fica na tela durante o salto.
- **Animes via torrent:** duracao real (o episodio nao e mais marcado como visto
  no comeco), audio sem engasgos em arquivos com quadros-chave espacados.
- **Legendas:** duas falas simultaneas aparecem juntas (ate 4 linhas).
- **Painel Episodios:** direcional esquerdo abre a lista da serie; escolha qualquer
  episodio, inclusive o anterior.
- **Faixas com nome do idioma** em vez de `audio_0`.

Detalhes, medicoes e roteiro de teste: `docs/PLAYER_STABILITY_0_12_45.md`.
Ambiente de teste do player real no Linux: `tools/host_player/README.md`.
