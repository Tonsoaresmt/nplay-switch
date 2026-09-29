# Nplay Switch 0.12.28

Correcao emergencial da abertura do player e da selecao inicial de audio apos
o teste da 0.12.27 no Nintendo Switch.

## Tela de preparacao sem piscar

- Abertura, reproducao e operacoes internas agora possuem um dono explicito da
  tela. Assim que a thread de demux inicia, o callback do FFmpeg continua
  podendo interromper a rede, mas deixa de apresentar o loader.
- Isso impede `Preparando video` e `Aguardando dados` de limparem e apresentarem
  a mesma tela alternadamente antes do primeiro quadro.
- Seek e troca de faixa conservam uma tela propria enquanto a operacao sincrona
  esta ativa; a correcao nao remove feedback nem cancelamento por `B`.

## Portugues respeitado na entrada normal

- A variante-base recebida no detalhe da serie nao sobrescreve mais a
  preferencia da conta. Com `Audio preferido: Dublado`, uma fonte multiaudio
  abre em PT-BR mesmo se o catalogo tiver entrado pela variante Legendada.
- Uma mudanca intencional de versao por `ZL/ZR` continua tendo prioridade. A
  escolha manual dentro do player tambem continua preservada em uma reabertura
  da mesma reproducao.
- A regra foi isolada em `audio_effective_preference` e cobre Dublado,
  Legendado, Tanto faz e selecao explicita em teste nativo.

## Validacao

- `test_player_loading` reproduz a disputa da 0.12.27 e comprova que o callback
  nao desenha durante a reproducao, nem antes do primeiro quadro.
- `test_audio_policy` comprova que Dublado vence uma variante-base Legendada e
  que uma troca explicita ainda e respeitada.
- O build ARM64 usa `-Wall -Wextra -Werror`. A validacao completa tambem cobre
  HLS multifaixa, WebVTT, seek, relogio, episodios e remux.
- Artefato final: `Nplay.nro`, 24.160.079 bytes.
- SHA-256: `1b58ad6d8d51fcee844f36d2f0c2796767aed2c0d22080818ed3a98a2ef05040`.

O teste final de apresentacao e selecao das faixas ainda precisa ser realizado
no Switch real com a mesma rota: Series, X-Men, episodio multiaudio.
