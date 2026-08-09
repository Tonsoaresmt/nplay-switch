# Continuidade para agentes

## Objetivo atual

O foco e otimizar o homebrew Nplay para Nintendo Switch sem trocar a arquitetura SDL2/FFmpeg/libcurl existente. Priorize fluidez da UI, uso previsivel de memoria e estabilidade do streaming.

## Estado das otimizacoes

- `source/main.c`: cache de capas usa hash para lookup, fila de surfaces prontas e limite LRU de 160 texturas.
- `source/net.c`: respostas HTTP crescem geometricamente para reduzir `realloc` e fragmentacao.
- `source/player.c`: buffer PCM e reutilizado entre frames.
- Validacao em 08/08/2026: `make -j4` concluiu sem erros nem avisos e regenerou `Nplay.nro`.
- Pendente: teste no Switch real de navegacao longa, retorno a capas expulsas do LRU e reproducao/seek com audio.

## Player reorganizado em 09/08/2026

- HUD novo em tres zonas: titulo/estado, progresso/status e ferramentas.
- Revisao visual posterior: durante a reproducao o HUD e compacto e mostra apenas
  pausa, saltos, `+ Mais controles` e voltar. Ao pausar ou abrir com `+`, o painel
  completo mostra audio, legenda, volume e todos os atalhos com espacamento proprio.
- Status visiveis para volume, idioma do audio, legenda e modo do HUD.
- Controles: `A` pausa/continua, `L/R` 10s, `ZL/ZR` 60s, `Y` audio,
  `X` legenda, `cima/baixo` volume, `+` alterna HUD automatico/fixo e `B` volta.
  `-` continua sendo uma saida rapida por compatibilidade.
- Pausa e buffering possuem cards centrais; a abertura usa tela de preparacao.
- Validacao local: `make -j4` concluiu sem erros nem avisos e regenerou `Nplay.nro`.
- Pendente no hardware: conferir se todos os textos cabem em 1280x720, alternar todas
  as faixas de audio/legenda, testar buffer lento e confirmar o comportamento do `+`.
- Uma captura recebida em 09/08 mostrava o HUD antigo (`Audio: ... Leg: ... A pausa`
  numa linha sobreposta). Essa string foi confirmada ausente no `Nplay.nro` atual;
  se reaparecer no console, o arquivo instalado nao foi substituido pelo build novo.
- Segunda revisao visual apos teste no hardware: o painel expandido passou a ocupar
  282 px e foi separado em progresso, transporte e configuracoes. Volume, idioma,
  legenda e modo do painel agora usam quatro cards de 284 px, evitando cortes como
  `VOLUME 1`, `LEGENDA OF` e `PAINEL FIX`. Build novamente validado sem avisos.
- Revisao de logica posterior: volume limitado a 0..100 e persistido em
  `sdmc:/switch/Meruem/player_volume.txt`; mudancas de volume, audio, legenda e seek
  exibem feedback contextual mesmo com HUD compacto. Seek agora parte de `cur_pos`
  e so altera clocks/buffers quando `av_seek_frame` confirma sucesso.
- Abertura de novos decoders de audio/legenda e transacional (falha nao destroi a
  faixa atual). A legenda escolhida automaticamente agora abre o decoder no inicio;
  antes ela aparecia selecionada, mas nao era decodificada ate o primeiro `X`.
- Cards mostram a posicao da faixa (`AUDIO 2/3`, `LEGENDAS 1/2`) e o card de pausa
  central redundante foi removido. Build final validado sem erros nem avisos.
- Selecao multipla revisada: `Y` abre modal de audio e `X` abre modal de legendas,
  com idioma, titulo da faixa, codec, canais, item atual, D-pad, A confirma e B
  cancela. O audio fica pausado e os clocks sao reancorados ao fechar, evitando
  salto no video pelo tempo gasto no menu.
- Energia: `play_with_progress` mantem `appletSetMediaPlaybackState(true)` desde a
  abertura da rede/FFmpeg ate qualquer saida, incluindo erros. A aba Salvos mantem
  a tela ativa apenas enquanto esta visivel e ha jobs ativos; os jobs sao do servidor
  e continuam mesmo com app fechado/console dormindo.
- A flag da aba Salvos e recalculada depois que o player fecha, pois ambos controlam
  o mesmo estado de energia. Falta de refresh por 30s libera a tela para evitar
  dreno infinito de bateria com estado antigo.
- Falhas de rede no player nao sao mais tratadas como fim natural/autoplay; retornam
  erro -5 e ainda salvam a posicao. Falha transitoria ao atualizar jobs preserva a
  ultima lista conhecida. Estados erro/error/failed/cancelled/canceled sao aceitos.

## Como continuar

1. Antes de editar, confira `git status --short` e preserve mudancas do usuario.
2. Compile com `make` usando o devkitPro configurado neste ambiente.
3. Nao considere a validacao concluida apenas pela compilacao: player, capas e navegacao devem ser testados no Switch quando possivel.
4. Ao encerrar uma rodada, atualize este arquivo se o estado ou os proximos passos mudarem.

## Proximos candidatos

- Medir no hardware o limite ideal de texturas de capas (atual: 160).
- Instrumentar tempo de frame, memoria livre e underruns de audio antes de alterar buffers/threads.
- Avaliar fila de audio com teto para impedir latencia crescente em fontes que decodificam muito a frente.

## Rodada 0.6.2 em 09/08/2026

- `source/curl_avio.c`: blocos de rede cairam de 4 MB para 512 KB e o ring de
  prefetch de 32 MB para 16 MB. Partes recebidas antes de uma queda passam a ser
  preservadas e retomadas; o timeout total de 60 s foi substituido por deteccao de
  conexao parada. Depois da abertura, uma espera vazia devolve `EAGAIN` em 300 ms,
  permitindo desenhar buffering e processar controles.
- Esta correcao ataca diretamente o padrao observado em *Descendants of the Sun*:
  fontes lentas nao precisam mais concluir 4 MB em 60 s ou recomecar o bloco.
  Ainda e obrigatorio confirmar esse titulo no Switch real e registrar fonte,
  codec, bitrate e timestamp se persistir.
- `source/screen_movie.c`: sinopse agora quebra em varias linhas e pode ser rolada;
  metadados, direcao, acoes e elenco com fotos ocupam secoes definidas. Quando a
  API envia `related`, `R/L` alterna entre elenco e titulos relacionados.
- O backend `Tonsoaresmt/Nplay` recebeu localmente a montagem de `item.related`
  usando apenas filmes reproduziveis do catalogo, priorizando franquia, diretor e
  genero. Esse repositorio deve ser publicado/deployado junto para a guia aparecer.
- A aba Salvos distingue servidor e microSD. `Y` copia um item pronto para
  `sdmc:/switch/Nplay/downloads`, com progresso, cancelamento por B e tela ativa;
  `A` prefere a copia offline e `ZR` a remove. O player abre caminhos `sdmc:/`
  diretamente pelo FFmpeg, sem alocar o buffer de rede.
- Configuracoes nao exibem mais hostname, caminho, capacidade ou consumo interno
  do servidor. Mostram apenas contagem de itens da conta e copias offline locais.
- Versao preparada: 0.6.2. Build local concluido sem erros nem avisos; falta teste
  funcional no hardware (player lento, seek, troca de faixas, download/cancelamento,
  reproducao offline, tela de filme e atualizacao a partir de uma instalacao antiga).

## Instalador corrigido em 09/08/2026

- Causa do falso sucesso: quando o hbmenu nao fornecia `argv[0]`, o fallback apontava
  para o nome antigo `sdmc:/switch/Meruem.nro`; a copia podia ser criada/atualizada,
  mas o usuario reabria outro `Nplay.nro`.
- `source/update.c` agora usa temporario em `sdmc:/switch/.nplay-update.download`,
  aceita o alvo apenas se existir e procura `Nplay`/`Meruem` em `sdmc:/switch` e
  nas pastas imediatamente abaixo.
- Todas as copias encontradas sao atualizadas por staging `.new` + troca atomica;
  o tamanho gravado e verificado antes da ativacao.
- A UI diferencia sucesso completo, parcial e falha. Nao anuncia mais sucesso se
  nenhum `.nro` instalado foi localizado.
- Build validado sem erros nem avisos e `Nplay.nro` regenerado.
- Bootstrap necessario: uma instalacao que ainda executa o atualizador antigo deve
  receber este `Nplay.nro` manualmente uma vez; so depois as proximas releases usam
  o instalador corrigido.
