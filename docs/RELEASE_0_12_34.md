# Nplay Switch 0.12.34

## Touch direto no modo portatil

- O toque deixou de converter arrastes em passos de cima/baixo/esquerda/direita.
- `SDL_FINGERMOTION` move o conteudo durante o gesto, com coordenadas absolutas,
  trava de eixo depois de 12 px e inercia curta ao soltar.
- Tremores pequenos continuam sendo um toque; arrastar nunca abre a obra ao soltar.
- Somente o primeiro dedo controla o gesto. Um segundo contato e ignorado ate a
  conclusao do primeiro, evitando selecao acidental.
- Tocar numa capa, botao, filtro, temporada, episodio, perfil ou configuracao
  continua executando a acao diretamente no ponto tocado.

## Superficies revisadas

- Home: pagina vertical, destaques, prateleiras horizontais e abas.
- Busca, sagas, Biblioteca e listas pessoais: grades verticais continuas.
- Historico: `Continuar assistindo` e listas com rolagem horizontal real.
- Series: temporadas e episodios independentes; tocar no episodio abre-o.
- Filmes e sagas: titulos relacionados usam rolagem horizontal persistente.
- Seletor de avatares: gesto lateral troca de pagina sem simular L/R na fila SDL.
- Player: timeline permanece absoluta; no painel de audio e legendas, tocar numa
  faixa seleciona e aplica pela mesma rotina transacional do controle. Tocar fora
  fecha o painel.

## Protecoes

- Eventos de mouse sintetizados por touch foram desativados antes de iniciar SDL.
- O foco e aproximado do item visivel ao terminar um arraste para que a troca de
  touch para Joy-Con seja previsivel, sem confirmar nem abrir conteudo.
- Qualquer botao do controle cancela a inercia imediatamente.
- O reconhecedor e isolado de SDL e possui simulacoes para jitter, tap, lock de
  eixo, velocidade, arraste e ownership do dedo.

## Validacao

- Build Nintendo Switch ARM64 com warnings tratados como erro.
- `tools/test_touch_input.c` cobre tap, tremor, dois dedos, eixo horizontal,
  eixo vertical e velocidade.
- `tools/validate_release.ps1` impede a volta do antigo swipe-direcional e valida
  movimento continuo, inercia, superficies e toque direto no painel do player.
- Artefato: `Nplay.nro`, 24.176.463 bytes, SHA-256
  `1faf37b4fd7d9b1a4a1731ad100c55a8a62c125e1eb80650af5525c44b6cc16b`.
- Pendente fisico: testar em modo portatil tap/arraste/diagonal/fling nas telas
  acima, bordas das listas, troca touch -> Joy-Con, painel de faixas e timeline.

## Referencias de implementacao

- SDL2 touch: https://wiki.libsdl.org/SDL2/README-touch
- Eventos touch gerando mouse: https://wiki.libsdl.org/SDL2/SDL_HINT_TOUCH_MOUSE_EVENTS
- Estrutura absoluta do touchscreen no libnx:
  https://switchbrew.github.io/libnx/hid_8h_source.html
