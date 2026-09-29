# Nplay Switch 0.12.27

Esta versao aproxima o fluxo de episodios do player do navegador sem alterar a
pipeline SDL2/FFmpeg/libcurl consolidada na 0.12.26.

## Proximo episodio dentro do player

- O HUD ja possuia o desenho de proximo episodio, mas ele estava desligado e nao
  executava nenhuma acao. Agora series com um episodio seguinte exibem o botao.
- Direcional direito abre o cartao e `A` confirma. `B`, `-` ou direcional
  esquerdo cancelam o cartao sem fechar o video. A tela sensivel ao toque tambem
  separa as areas de faixas e de proximo episodio.
- Nos 45 segundos finais o cartao aparece automaticamente, com progresso visual,
  mesmo quando o restante do HUD esta oculto. Legendas sao elevadas para nao
  ficarem cobertas.
- A escolha manual passa pelo mesmo encadeamento de episodios e pela mesma
  continuidade de idioma da serie. Ela nao depende de autoplay estar habilitado
  e nao mostra uma segunda confirmacao redundante.
- Sair para o proximo episodio salva a posicao atual. A API continua decidindo a
  conclusao pelo progresso real; pular cedo nao marca artificialmente o episodio
  como assistido.

## Estabilidade e verificacao

- O pedido de proximo episodio e um motivo de saida explicito do player. Ele so e
  entregue ao fluxo da serie depois da limpeza normal de demux, decoders, audio,
  textura, heartbeat e sessao atual.
- `source/player_next.c` concentra a janela e a animacao do cartao em logica pura.
  `tools/test_player_next.c` testa indisponibilidade, entrada nos 45 segundos,
  metade da contagem e selecao manual quando a duracao ainda nao e conhecida.
- O build ARM64 continua com `-Wall -Wextra -Werror` e a suite completa inclui os
  fixtures de HLS multifaixa/WebVTT, seeks repetidos, remux, audio PT-BR, relogio
  apos pausa e ordem de episodios.
- Artefato final: `Nplay.nro`, 24.160.079 bytes.
- SHA-256: `7532c5fe37c2d3e48037ea55ea7a7b1be831bfaa50aaf1208e5534f156aa2f25`.

## Limite da comprovacao

O artefato e validado localmente, mas este ambiente nao executa Horizon nem a GPU
do Switch. No hardware, testar uma serie com pelo menos dois episodios: abrir o
cartao com direcional direito, cancelar, abrir de novo, confirmar, verificar a
continuidade PT-BR/legenda e deixar outro episodio chegar aos 45 segundos finais.
