# Nplay Switch 0.12.22

## Correcoes desta rodada

- Legendas agora usam uma fila limitada e temporizada. Cues futuros nao aparecem
  antes da hora, falas sobrepostas podem coexistir e itens vencidos sao removidos.
- Os tempos `start_display_time` e `end_display_time` do FFmpeg sao respeitados;
  legendas sem duracao recebem um fallback seguro de quatro segundos.
- Quebras de linha ASS/WebVTT sao preservadas e o texto passa exclusivamente pelo
  HUD moderno, inclusive com o HUD oculto, pausado, em buffering ou no painel de
  audio e legendas.
- O cache visual agora detecta mudanca no conteudo da legenda, evitando que uma
  fala anterior continue desenhada quando o mesmo buffer recebe outro texto.
- Trocas de audio e legenda tornaram-se transacionais: o decoder atual permanece
  valido enquanto o novo decoder abre e sincroniza. Se a abertura ou o seek HLS
  falhar, a faixa anterior e restaurada em vez de deixar o player sem saida.
- A nova simulacao `test_subtitle_queue` foi incorporada a validacao de release.

## Verificacao local

- Suite limpa completa concluida sem erros nem avisos.
- Contratos site/Switch, API de playback, relogio, politica de audio, episodios,
  remux e simbolos obrigatorios aprovados.
- Fila de legendas aprovada para cue futuro, sobreposicao, expiracao, duracao
  ausente e reset apos seek/troca.
- Artefato: `Nplay.nro`, 24.143.695 bytes,
  SHA-256 `cfed5827462830a26f52d0ab428ee6351df9122159a08c0a1b7607b99fd3bb96`.

## Validacao ainda necessaria no Switch

- WebVTT e ASS reais, incluindo duas falas simultaneas.
- Seek enquanto uma legenda esta visivel.
- Ativar, desativar e alternar varias faixas de legenda.
- Trocar audio no meio de filme e episodio R2.
- Forcar falha de uma rendition HLS e confirmar que a faixa anterior continua.
