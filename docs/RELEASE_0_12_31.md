# Nplay Switch 0.12.31

## Problemas reproduzidos pelas capturas

- O mesmo master HLS mostrava tres legendas no navegador e nenhuma no Switch.
- Cada toque em L/R/ZL/ZR iniciava uma busca de rede; saltos sucessivos reabriam
  a pipeline quando uma busca expirava, dando a impressao de preparar todo o video.
- A preferencia de audio Dublado foi confirmada pelo usuario e ja abre PT-BR; esta
  rodada nao volta a alterar a politica de idioma validada na 0.12.30.

## Correcao

- O manifesto raiz e lido antes do demux e suas renditions `TYPE=SUBTITLES` sao
  preservadas com nome, idioma e URI.
- Quando o FFmpeg do Switch nao cria AVStreams de legenda, o player usa essas
  renditions como fallback. Ao escolher uma faixa, baixa e decodifica somente a
  playlist WebVTT, sem reiniciar video ou audio.
- A URL efetiva apos redirects e usada como base e o token assinado do master e
  herdado por referencias relativas.
- L/R e ZL/ZR abrem a pre-visualizacao de seek. Toques seguintes acumulam o alvo;
  `A` confirma uma unica busca e `B` cancela sem mudar a reproducao.

## Validacao local

- Build ARM64 completo com `-Wall -Wextra -Werror`.
- `tools/validate_release.ps1` completo.
- Contrato site/Switch, parser HLS, politica PT-BR, fila WebVTT, clock/sync,
  proximo episodio, hot API e fluxo de episodios.
- Remux fragmentado: 203 quadros.
- Fixture HLS: cinco faixas, duas legendas, cues no inicio e depois de seek.
- Artefato: `Nplay.nro`, 24.164.175 bytes, SHA-256
  `9d1b83897b0cb1a3c19a3a1393a040277d5ed6f468fe83d73e72bf2ef6e289a4`.

## Validacao que continua obrigatoria no hardware

1. Abrir exatamente a obra das capturas e conferir as tres legendas portuguesas.
2. Ativar cada faixa, buscar inicio/meio/fim e confirmar texto no tempo correto.
3. Alternar Japones -> Portugues -> Japones por cinco ciclos e assistir por 15 min.
4. Pressionar ZR varias vezes, confirmar uma vez com A e medir o tempo ate retomar.
5. Repetir com a fonte direta e com a copia R2; fotografar Diagnostico se houver
   fechamento, pois NVDEC, SDL Audio e Wi-Fi nao podem ser executados no PC.
