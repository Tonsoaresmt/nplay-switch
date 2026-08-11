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

## Preparacao para reproducao em 10/08/2026 (0.6.3)

- `resolve_and_play` nao redireciona mais filmes/torrents para a aba Salvos.
  Depois de solicitar o preparo no acelerador, permanece no contexto atual e abre
  uma tela de espera; quando `ready=true`, inicia o player automaticamente.
- A espera mostra animacao continua, percentual real, bytes preparados, tamanho,
  velocidade, previsao restante e numero de fontes quando a API disponibiliza.
- O polling de status roda numa thread com timeout curto; lentidao/reconexao da API
  nao congela a animacao nem impede que o usuario pressione `B`.
- O texto diferencia explicitamente preparo no servidor de download no Switch:
  nenhum espaco da microSD e ocupado por esse fluxo. Download offline continua
  sendo uma acao separada (`Y` na aba Salvos).
- `B`/`-` sai da espera sem cancelar o job; o servidor continua trabalhando e o
  item permanece acessivel em Salvos. Erros definitivos do job encerram a espera
  com a mensagem retornada pelo servidor.
- A tela fica ativa durante toda a espera. Ao iniciar o player ou voltar, a flag
  de energia e restaurada e recalculada pelo loop principal.
- Build local de `source/main.c` validado em 10/08/2026 sem erros nem avisos.
  Pendente no hardware: conferir fluidez da animacao durante polling, legibilidade
  dos tres cards e transicao automatica para o player em job novo e ja pronto.

## Auditoria de lag e estabilidade em 10/08/2026 (0.6.4)

- Removido I/O da microSD por frame na aba Salvos. Os IDs offline agora ficam em
  memoria e a pasta e relida apenas ao entrar na aba, concluir ou remover download.
- A lista de jobs deixou de bloquear o loop principal a cada dois segundos: fetch e
  parse JSON rodam em thread com timeout, e o resultado e aplicado no frame seguinte.
- A sinopse do filme e quebrada/medida uma unica vez ao abrir o detalhe, nao 60 vezes
  por segundo. Isso reduz rasterizacao de texto e churn no cache de fontes.
- Criacao de texturas de capas foi limitada a duas por frame. Workers de capa usam
  timeout proprio de 15 s para uma URL morta nao ocupar a fila por 45 s.
- O cache de metadados de capas agora recicla entradas LRU ociosas ao atingir 3.000
  URLs. Antes, capas novas ficavam vazias permanentemente ate reiniciar o app.
  Filas de download e surfaces prontas passaram a contar ocupacao explicitamente,
  evitando a ambiguidade `head == tail` quando circulares ficam cheias.
  Criacao e expulsao de texturas ocorrem antes do desenho, evitando destruir uma
  textura que ja havia sido enviada ao renderer no mesmo quadro.
- Player valida codec, contexts, textura, conversor, frames e packets antes de usar;
  formatos sem decoder e falhas de memoria retornam erro em vez de acessar NULL.
- Frames com mais de 120 ms de atraso sao descartados antes de conversao/upload para
  recuperar sincronismo, em vez de gastar CPU/GPU desenhando quadros vencidos.
- Timestamps de audio/video/legenda e seek sao normalizados por `fmt->start_time` e
  usam `best_effort_timestamp`. Corrige fontes TS/HLS cujo relogio interno nao inicia
  em zero e que podiam parecer congeladas ou muito lentas.
- Reconfiguracao do resampler verifica `swr_init` e libera layouts temporarios. Se a
  saida SDL de audio falhar, o decoder nao continua consumindo CPU sem produzir som.
- Endpoints JSON da UI agora tem teto de 15/20 s em vez de congelar ate 45 s.
- Cada reproducao grava `sdmc:/switch/Meruem/player_stats.txt` com resolucao, frames
  decodificados/descartados, eventos de buffering, maior fila de audio e erro final.
  Esse arquivo deve acompanhar relatos futuros de travamento.
- Build validado sem erros nem avisos. Pendente no hardware: navegacao por mais de
  3.000 capas, Salvos durante perda de rede, TS/HLS com origem nao zero e comparacao
  de `player_stats.txt` entre um video fluido e um problemático.

## Auditoria visual geral em 10/08/2026 (0.6.5)

- `source/ui.c` concentra agora cabecalho de 72 px, rodape de 52 px, paineis com
  barra de destaque, foco, badges, progresso, estado vazio e alinhamento de texto.
  Novas telas devem reutilizar esses helpers em vez de criar medidas isoladas.
- Login foi reconstruido como card central com acao principal, explicacao de
  seguranca, erro contido e saida separada. Configuracoes usam dois cards claros
  para diferenciar itens da conta e arquivos offline na microSD.
- Inicio, busca, series, seletor de episodios e Salvos compartilham margens de
  40 px, hierarquia de titulo/subtitulo, foco com barra lateral e rodape fixo.
  O calculo de scroll reserva o rodape, evitando que a selecao fique escondida.
- Cards de catalogo incluem uma area propria para titulo e o foco cobre capa e
  texto. Telas vazias, carregando catalogo e atualizando Salvos ganharam mensagens
  centralizadas com contexto, em vez de texto solto no canto.
- Detalhes de filme ganharam poster em painel, metadados/sinopse organizados,
  botoes com rotulo centralizado, divisao clara de elenco/relacionados e rodape.
- A tela de serie organiza poster/metadados num painel lateral. Varias versoes de
  audio aparecem como opcao atual + posicao (`1/2`) e instrucao `ZL/ZR`, sem uma
  sequencia de rotulos capaz de ultrapassar a coluna.
- Espera do servidor, resolucao inicial e download offline foram alinhados ao mesmo
  sistema visual. O fluxo offline explica novamente que ocupa a microSD.
- Player teve controles compactos/expandidos redistribuidos em grades regulares.
  Legendas longas agora quebram em ate duas linhas, em vez de serem comprimidas e
  deformadas horizontalmente.
- Pendente no hardware: capturar login, Inicio com 2+ rails, busca com duas linhas,
  serie com audio duplo, filme com elenco/relacionados, Salvos cheio/vazio, download
  offline e HUD compacto/expandido. Conferir overscan e legibilidade a distancia.

## Historico, listas e reinicio em 10/08/2026 (0.6.6)

- A antiga aba `Salvos` virou `Historico`. `/api/sync/progress` e carregado em uma
  thread propria e mostra filmes/episodios em andamento com barra de progresso;
  `A` resolve o item diretamente e retoma da posicao sincronizada.
- O inicio do Historico tem uma segunda linha de atalhos. `Biblioteca` preserva os
  jobs preparados e downloads offline, mas detalhes de infraestrutura deixaram de
  aparecer. Estados de espera usam mensagens de streaming e dicas rotativas.
- Listas pessoais sao persistidas em `sdmc:/switch/Meruem/media_lists.json`, com
  limite atual de 8 listas e 64 itens por lista. A primeira instalacao cria
  `Assistir mais tarde`; criar, renomear, excluir (com confirmacao), abrir e remover
  itens pode ser feito no Historico. Estas listas sao locais: o backend atual nao
  possui API para listas arbitrarias; sincronizacao entre aparelhos e futura.
- Filmes aceitam `Y` para inclusao rapida em `Assistir mais tarde` e `X` para uma
  lista nomeada. Series usam `+` para uma lista nomeada sem conflitar com preparar
  episodios (`Y`) ou Minha lista (`X`).
- O detalhe de filme nao mostra mais Elenco. Relacionados aparecem por padrao,
  aceitam foco horizontal, `A` abre a obra, `Y` adiciona a Assistir mais tarde e
  `X` adiciona/cria outra lista. A troca de filme e transacional: falha de API nao
  apaga o detalhe que ja estava aberto.
- Configuracoes ganharam `Reiniciar Nplay` e `Fechar Nplay`. Quando hbloader oferece
  `envSetNextLoad`, reiniciar agenda o NRO atual e encerra depois de 1,4 s. Uma
  atualizacao completa tambem reinicia automaticamente; carregadores sem suporte
  recebem mensagem clara e continuam exigindo reabertura manual.
- `update_resolve_target_path` valida `argv[0]`, prioriza caminhos conhecidos e so
  depois varre a pasta `switch`, reduzindo o risco de reiniciar uma copia errada.
- Pendente no hardware: validar retorno automatico pelo hbmenu usado no console,
  Historico com filmes e episodios, listas com muitas capas, teclado de nome/confirmacao
  e navegacao por mais de dez relacionados.

## Relacionados, Historico e timeline em 10/08/2026 (0.6.7)

- `source/screen_movie.c`: relacionados compactos usam capas maiores e, ao receber
  foco com baixo, sobem em um painel animado que ocupa mais da metade inferior da
  tela. A selecao usa capas de 152x216, titulo completo em destaque, contador e
  acoes contextuais; cima recolhe o painel sem perder a obra selecionada.
- `source/main.c`: Continuar assistindo usa posters de 168x224. Biblioteca, listas
  locais e criacao de lista compartilham dimensoes, espacamento e hierarquia visual,
  eliminando os cards irregulares da primeira versao do Historico.
- `source/player.c`: mover horizontalmente o analogico esquerdo abre uma busca pela
  timeline. A inclinacao controla a velocidade, `A` confirma e `B` cancela. O modo
  apenas calcula uma pre-visualizacao local e executa um unico `av_seek_frame` ao
  confirmar, evitando bombardear fontes lentas com seeks durante o movimento.
- O mesmo helper transacional de seek agora atende timeline e saltos L/R/ZL/ZR:
  codecs, legendas e fila de audio so sao limpos depois que FFmpeg aceita a busca.
- Pendente no hardware: validar zona morta e sentido do analogico nos Joy-Con,
  velocidade da busca em videos curtos/longos, painel relacionado com 1 e 10+ itens,
  legibilidade dos cards do Historico e carregamento tardio das capas expandidas.

## Recuperacao, diagnostico e sincronizacao em 10/08/2026 (0.6.8)

- Historico ganhou menu por `X`: continuar, recomecar, marcar concluido e remover.
  As duas ultimas acoes atualizam a API e retiram o card imediatamente da tela.
- `Assistir mais tarde` agora usa `/api/sync/watchlater`: itens da conta sao
  mesclados na lista local ao carregar o Historico, inclusoes sao espelhadas e uma
  remocao so e aplicada localmente depois da confirmacao remota. A lista sincronizada
  mantem nome fixo; as demais colecoes continuam locais e editaveis.
- Relacionados pre-carregam somente os dois vizinhos de cada lado da selecao. O
  mecanismo reutiliza a fila e o LRU existentes, sem alterar o teto de 160 texturas.
- `source/curl_avio.c` diferencia HTTP 4xx definitivo de queda transitoria. O erro
  antigo podia permanecer ativo mesmo depois da thread voltar a receber bytes;
  agora sucesso limpa a flag e quedas recebem uma janela de ate 120 s para recuperar.
- O card de buffering evolui de Carregando para Recuperando e informa `B` apos
  30 s, mantendo controles responsivos enquanto a thread tenta reconectar.
- Configuracoes ganhou `X Diagnostico do player`, que traduz `player_stats.txt` em
  resolucao, quadros descartados, bufferings, fila maxima de audio e resultado
  amigavel. Relatos devem incluir foto dessa tela, titulo e momento do problema.
- A busca pela timeline mostra capitulos embutidos do arquivo e usa cima/baixo para
  saltar entre eles. Fontes sem metadados continuam com busca analogica normal.
- Miniaturas de seek nao foram geradas no console: isso exigiria seeks/decodes extras
  em fontes lentas. Proximo caminho seguro e um endpoint de sprite WebP/JPEG com
  intervalos e timestamps; o Switch deve manter no maximo um sprite pequeno em RAM.
- Listas nomeadas ainda exigem backend futuro (`collections`, `collection_items`,
  CRUD por perfil e `updated_at` para merge). Nao simular sincronizacao delas no
  cliente ate esse contrato existir.
- Pendente no hardware: todas as pendencias de 0.6.7 mais queda de rede por 10/40/120 s,
  diagnostico apos saida/erro, arquivo com capitulos e Assistir mais tarde em dois aparelhos.

## Protecao do analogico em 10/08/2026 (0.6.9)

- Relato de hardware: o analogico esquerdo abria a timeline com um toque pequeno,
  pausando a experiencia e movendo a pre-visualizacao de forma brusca.
- A timeline agora exige eixo horizontal acima de 24.500 (cerca de 75% do curso)
  mantido por 550 ms. Entre 280 e 550 ms o HUD apenas informa para continuar
  segurando; nenhum pause, preview ou seek acontece antes da ativacao intencional.
- A zona morta dentro da timeline subiu de 8.000 para 14.000 e so e rearmada depois
  que o eixo volta abaixo de 9.000. Confirmar/cancelar com o stick ainda inclinado
  nao pode reabrir o painel imediatamente.
- A velocidade proporcional caiu de 1,2-8,0% para 0,6-4,5% da duracao por segundo.
  `A` continua sendo a unica forma de executar `av_seek_frame`; `B` cancela e volta
  exatamente ao ponto anterior. O modal explicita que nada muda sem confirmar.
- Pendente no hardware: validar Joy-Con com drift leve, Pro Controller, toque curto,
  segurada intencional, confirmacao/cancelamento ainda inclinado e videos de 20/120 min.

## Retorno contextual em 10/08/2026 (0.6.10)

- Causa da perda de contexto: `input_movie` e `input_series` atribuiam sempre
  `SC_MAIN` ao pressionar `B`, mesmo quando o detalhe havia sido aberto pela busca.
- `detail_capture_origin` registra `SC_SEARCH` ou `SC_MAIN` antes de abrir um detalhe;
  `detail_return_to_origin` restaura essa tela sem destruir consulta, selecao, scroll,
  aba, rail, lista pessoal ou subvista da Biblioteca.
- `open_item` cobre Home/rails/busca e a abertura direta por lista tambem captura a
  origem. Series mantem a origem durante troca de audio, temporada ou versao agrupada.
- Filmes relacionados usam uma pilha local de ate seis detalhes. Cada nivel preserva
  o JSON ja carregado, acao selecionada, scroll da sinopse, relacionado selecionado
  e altura do painel. `B` volta primeiro ao filme anterior sem nova requisicao; ao
  esvaziar a pilha, retorna a pesquisa/lista/aba original.
- O limite de seis evita crescimento de memoria em navegacao indefinida. Ao exceder,
  o nivel mais antigo e liberado; os seis retornos mais recentes continuam disponiveis.
- Pendente no hardware: busca -> filme/serie -> B, lista -> detalhe -> B, cadeia com
  2/7 relacionados, playback no meio da cadeia e troca de audio de serie antes de voltar.
