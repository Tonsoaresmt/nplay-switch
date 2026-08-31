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

## Conta, descoberta e Biblioteca em 11/08/2026 (0.6.11)

- A interface offline foi recolhida ate a funcionalidade estar pronta para o
  publico. Biblioteca mostra somente obras preparadas; botoes, badges e contadores
  de microSD deixaram de ser anunciados. O codigo de copia local foi preservado e
  marcado como reservado para a rodada futura, sem ficar acessivel pela UI.
- Configuracoes agora consulta `/api/auth/me` e apresenta o nome real do plano,
  estado da assinatura, limite de telas simultaneas, dispositivos e validade. Se o
  backend nao enviar `current_period_end`, a UI informa que a data nao foi definida
  em vez de inventar vencimento.
- Plano e biblioteca ocupam cards simetricos na secao `CONTA E BIBLIOTECA`. As
  consultas rodam em thread; a tela continua respondendo enquanto os dados chegam
  e a conta aparece antes da consulta mais lenta de preparados terminar.
- O final de Inicio, Filmes, Series, Animes e Doramas ganhou um card de descoberta
  focavel. Baixo a partir da ultima prateleira leva a `MAIS NO NPLAY`; A ou Y abre
  a busca, com texto contextual por categoria. Isso torna explicito que as rails
  sao uma selecao e nao o limite do catalogo.
- A decisao visual segue padroes de interfaces de TV pesquisados em documentacao
  oficial: foco claramente destacado, acao confirmada separada da navegacao,
  legibilidade a distancia e busca visivel no contexto em que o catalogo termina.
- Versao preparada: 0.6.11. Pendente no hardware: capturar Configuracoes com plano
  ativo, teste, sem validade e indisponivel; navegar ate a busca final em todas as
  categorias; confirmar que nenhum comando offline aparece e que o card final nao
  sofre corte ou overscan em 1280x720.

## Alinhamento com o site em 21/08/2026 (0.7.0)

- O contrato atual de `C:/iptv` foi comparado com o cliente Switch. Foram trazidos
  apenas recursos adequados a uma interface de TV e controle, mantendo
  SDL2/FFmpeg/libcurl e sem copiar dependencias ou codigo de navegador.
- Inicio agora consome `jogos`, `trendingMovies`, `trendingSeries` e `liveShelves`.
  Filmes, Series e Doramas priorizam a nova rail `prontos`; `emAlta` e aceito quando
  o backend o enviar. Payloads antigos continuam funcionando porque rails ausentes
  sao simplesmente ignoradas.
- Cards interpretam `r2_ready`, `is_cam`, `year` e `kind`, exibindo badges PRONTO,
  CAM e AO VIVO. A classificacao deixou de depender somente da rail: episodio,
  filme, serie e canal seguem o fluxo correto mesmo quando aparecem misturados.
- Destaques usam `backdrop` landscape com recorte proporcional (cover), camada de
  contraste, sinopse curta, ano e disponibilidade. Sem backdrop, o poster e layout
  anterior permanecem como fallback. A rotacao usa no maximo oito obras, como o
  site, e `reduceMotion` desliga a troca automatica.
- Busca ganhou filtros locais Tudo/Filmes/Series/Ao vivo via ZL/ZR. Canais nao sao
  mais abertos como detalhe de filme; tocam pelo resolvedor. `Y Nova busca`, que era
  anunciado mas nao tratado dentro dos resultados, passou a funcionar.
- Configuracoes usa `/api/account/me` (com fallback `/api/auth/me`) e sincroniza
  `hideAdult`, `autoplayNext`, `reduceMotion` e `audioPref` por
  `PUT /api/account/prefs`. Autoplay do cliente agora respeita a conta. Plano mostra
  tambem perfis usados/permitidos e prioriza `access_expires_at` para validade.
- Login monta JSON com cJSON, evitando quebra por aspas ou barras na senha. Cada
  instalacao nova gera e persiste um fingerprint aleatorio, em vez de todos os
  Switches usarem `nplay-switch`; tokens ja existentes so recebem o novo device ID
  depois do proximo login.
- Durante o player, uma thread leve envia heartbeat a cada 20 s para a sessao criada
  pelo backend. Ao sair, `/api/stream/:itemId/stop` encerra a sessao. Isso alinha a
  contagem de telas/dispositivos dos novos planos sem fazer rede no frame do player.
- Versao preparada: 0.7.0. Pendente no hardware: validar backdrop e badges com rede
  lenta, todas as rails novas, busca contendo filme/serie/canal, quatro preferencias,
  autoplay ligado/desligado, sessao ativa durante video de 30+ min e login novo em
  duas contas/Switches. Confirmar tambem compatibilidade com servidor ainda em 0.6.x.

## Correcao emergencial de catalogo e reproducao em 21/08/2026 (0.7.1)

- Relato apos a 0.7.0: filmes, series e animes deixaram de funcionar corretamente
  no Switch. A API publicada foi verificada de ponta a ponta: Inicio, Filmes,
  Series e Animes responderam HTTP 200 com colecoes preenchidas; a resolucao de
  um filme retornou `m3u8`, um episodio de serie retornou `m3u8` e um anime
  retornou `mp4`, todos HTTP 200. As sessoes de teste foram encerradas.
- A 0.7.0 iniciava `/api/account/me` em uma thread ao mesmo tempo em que a thread
  principal abria o catalogo. Essa disputa HTTPS foi removida do boot e do login;
  conta/configuracoes continuam sendo consultadas quando Config e aberta.
- O heartbeat novo da reproducao nao e mais enviado imediatamente ao entrar no
  player. O primeiro envio espera 20 segundos, preservando DNS, TLS e banda para
  a abertura do FFmpeg; depois continua a cada 20 segundos, dentro do TTL de 90 s.
- Falhas de `/api/stream/:id` deixaram de mostrar apenas uma mensagem generica. A
  interface agora inclui o codigo HTTP e a mensagem segura da API, ou o erro de
  rede, permitindo distinguir limite de telas, fonte indisponivel e conectividade.
- Build 0.7.1 concluido sem erros nem avisos e `Nplay.nro` regenerado. A causa e
  fortemente isolada a concorrencia introduzida no cliente 0.7.0, mas a confirmacao
  final exige instalar 0.7.1 no hardware e abrir um filme, uma serie e um anime.

## Correcao da abertura HLS em 21/08/2026 (0.7.2)

- Teste no Switch com 0.7.1 confirmou `Reproducao interrompida (erro -1)` em
  filmes e series. A API retornava corretamente `container=m3u8`; o erro ocorria
  dentro de `avformat_open_input` no cliente.
- Causa: `curl_avio` representa uma unica resposta HTTP e era usado para todo link
  remoto. HLS precisa que o demuxer abra a playlist, siga redirecionamentos e abra
  cada submanifesto/segmento; alem disso o player passava URL nula ao FFmpeg, sem
  base para resolver referencias. Esse caminho funciona para MP4/MKV, nao HLS.
- A biblioteca instalada foi inspecionada e contem `ff_hls_demuxer`,
  `ff_http_protocol` e `ff_tls_protocol` (backend TLS do libnx). Agora somente HLS
  usa a pilha HTTP+TLS nativa do FFmpeg, com timeout, reconexao e verificacao TLS
  desativada como no caminho libcurl. MP4/MKV, acelerador e arquivos locais mantem
  o fluxo anterior com `curl_avio`.
- O player recebe explicitamente o tipo `m3u8`; nao tenta inferir pela extensao,
  pois `/api/play/:id` nao termina em `.m3u8`. Falhas de abertura agora preservam
  a etapa e o texto de `av_strerror`, em vez de colapsar tudo para `erro -1`.
- Build 0.7.2 concluido sem erros nem avisos e confirmou por simbolos que HLS,
  HTTP e TLS estao no binario. Pendente no hardware: abrir filme e episodio de
  serie, aguardar pelo menos 30 s, testar seek e confirmar reconexao de segmento.

## Refinamento visual dos catalogos em 21/08/2026 (0.7.3)

- A escala dos cards foi comparada com `C:/iptv/public/css/app.css` e
  `home-netflix.css`. No Switch, `ui_badge` usava a fonte normal de 23 px e uma
  faixa de 30 px sobre posters de 150 px; por isso ano e `PRONTO` dominavam a
  capa e davam aspecto de prototipo.
- `text.c` ganhou um terceiro tamanho de 17 px exclusivo para metadados. Cards
  usam badges compactos de 22 px, fundo escuro e apenas uma barra colorida fina:
  ano/CAM/AO VIVO no canto superior e `Pronto` no inferior direito. `Na lista`
  aparece por extenso somente no card focado; nos demais vira um marcador fino.
- Posters das prateleiras passaram de 150x214 para 164x232, com gap de 14 px,
  aproximando a presenca visual dos cards de 168 px do site sem comprometer o
  scroll em 1280x720. Os calculos de scroll/descoberta foram atualizados juntos.
- O foco deixou de envolver capa e uma grande caixa de titulo. Agora usa sombra,
  contorno roxo discreto apenas no poster, titulo solto sobre o fundo e uma linha
  azul curta abaixo. As faixas solidas sob cards foram removidas.
- O mesmo tratamento foi aplicado a Home, busca, Continuar assistindo,
  Biblioteca, listas pessoais e Relacionados e miniaturas de episódios continuam quebrando a interface se
  o título do filme/série exceder 3 linhas de largura em vez de truncar.

## Rodada 0.8.0 - Sprint 1 (Sessão, HLS e Sobrevivência a Falhas)
- O player agora é um `Session Player` (via `player_run`) isolado da chamada à API.
- Segurança SSL reforçada no libcurl (1L/2L) e na reabertura de playlists via FFmpeg (tls_verify=1).
- Máquina de estado para tolerância a falhas implementada: se o FFmpeg cair sem ser EOF, a tela congela com estado `RECUPERANDO SESSÃO` e uma re-resolução da URL é disparada.
- O progresso de salvamento (histórico) e o _heartbeat_ de conexão da sessão foram migrados do `player_play` nativo e do `main.c` para dentro de uma thread isolada dedicada (`playback_heartbeat_thread`), parando de bloquear a _main_ thread por I/O síncrono e preservando o framerate de 60fps do decodificador de vídeo.
- Construção e links (`api.h`) verificados, `.nro` compilado sem _warnings_ relativos às assinaturas antigas.
- Versão e build empurrados para repositório (Git push). Aguardando testes pelo usuário no Nintendo Switch para confirmar resiliência da nova arquitetura e iniciar a Sprint 2 (Paridade de Experiência e Áudio/Legenda WebVTT).

- Capas e backdrops agora compartilham `ui_cover`, equivalente a `object-fit:
  cover` do site. Historico, Biblioteca, listas, series e relacionados preservam
  a proporcao da imagem e recortam o excesso em vez de esticar a arte ou rostos.
- Titulos nao sao mais truncados por quantidade de bytes antes do desenho; o
  renderer usa toda a largura e evita cortar no meio caracteres UTF-8 acentuados.
- Build 0.7.3 concluido sem erros nem avisos. Pendente no hardware: capturar Home,
  busca, Historico, Biblioteca, lista e relacionados; conferir contraste dos
  badges pequenos a distancia, overscan e sete cards de 164 px na primeira rail.

## Causa raiz do HLS confirmada em 21/08/2026 (0.7.4)

- Apos 0.7.2/0.7.3 ainda nao reproduzirem filmes e series, a cadeia autenticada
  foi testada fora do Switch com o mesmo `/api/stream`, `/api/play`, URL final e
  FFmpeg. Anime MP4 abriu normalmente (H.264/AAC 1280x720), isolando a falha ao HLS.
- Os manifests e segmentos existem: filme e serie responderam master/child HLS
  validos e o primeiro `.m4s` respondeu HTTP 206 como `video/iso.segment`. Porem o
  CDN entrega manifestos comprimidos com `Content-Range` baseado no tamanho
  comprimido. FFmpeg envia `Range: bytes=0-` por padrao e lia somente parte do
  texto (filme 447 de 646 bytes; serie 1809 de 3430), resultando em `Empty playlist`
  ou `Invalid data found when processing input`. O site nao envia esse Range.
- Um segundo bloqueio aparecia depois da leitura completa: o filtro conservador
  de extensoes recusava algumas URLs assinadas de submanifestos/segmentos. O teste
  passou com H.264/AAC 1920x1080 para filme e serie, incluindo dois audios e varias
  legendas na serie, ao combinar `seekable=0`, `http_seekable=0` e
  `allowed_extensions=ALL`.
- O codigo oficial do FFmpeg n7.1 foi conferido: `hls.c` documenta explicitamente
  `http_seekable=0` para servidores que nao aceitam Range e oferece
  `allowed_extensions=ALL`. A build do Switch (`Lavf 61.7.100`) contem as tres
  opcoes por inspecao de simbolos/strings.
- O player 0.7.4 aplica `seekable=0` ao manifesto inicial, `http_seekable=0` aos
  filhos e libera as URLs assinadas somente no caminho HLS. MP4/MKV, acelerador e
  offline continuam no AVIO libcurl anterior. Pendente no hardware: filme e serie
  por 30+ s, troca de audio/legenda e seek; registrar a nova mensagem exata se falhar.

## HTTPS nativo e NVTEGRA em 22/08/2026 (0.7.5)

- A foto do hardware revelou a mensagem exata `abrir playlist HLS: Protocol not
  found`. A causa foi confirmada no pacote oficial `switch-ffmpeg 7.1-5`: a receita
  usa `--disable-protocols` e habilita `file,http,ftp,tcp,udp,rtmp,tls,httpproxy`,
  mas omite `https`. Ter `ff_http_protocol` e `ff_tls_protocol` no NRO nao registra
  automaticamente `ff_https_protocol`.
- `vendor/ffmpeg-https/lib/libavformat.a` foi recompilada do FFmpeg 7.1 com os dois
  patches oficiais do devkitPro e somente `https` acrescentado a lista de protocolos.
  O Makefile prioriza essa biblioteca local e continua usando codec/util/sws/swr do
  port oficial da mesma versao. O ELF final contem `ff_https_protocol`,
  `ff_hls_demuxer` e `ff_h264_nvtegra_hwaccel`.
- `tools/build_ffmpeg_https.sh` reproduz o artefato, valida os tres SHA-256 da receita
  oficial e aceita tanto `switchvars.sh` quanto a instalacao atual sem esse arquivo.
  O build exige um compilador C host; neste ambiente foi instalado o pacote `gcc` do
  MSYS2 do devkitPro. Nao substituir a biblioteca global em `portlibs`.
- O player continha os aceleradores NVTEGRA, mas nunca criava
  `AV_HWDEVICE_TYPE_NVTEGRA`; por isso H.264/HEVC eram decodificados apenas pela CPU.
  Agora cria o dispositivo para esses codecs, mantem fallback de abertura por CPU e
  transfere quadros NVTEGRA antes da conversao YUV/SDL. A conversao swscale passou a
  ser criada pelo formato real do primeiro quadro, nao pelo `pix_fmt` prematuro do
  contexto.
- `player_stats.txt` registra `hardware_decode=1` quando um quadro NVTEGRA foi
  realmente recebido. Configuracoes > Diagnostico mostra `NVTEGRA ativo` ou
  `decodificacao por CPU`; o leitor continua aceitando arquivos antigos sem o campo.
- Build limpo 0.7.5 concluido sem erros nem avisos do aplicativo e regenerou
  `Nplay.nro`. Pendente obrigatorio no Switch: filme e serie HLS por pelo menos 10
  minutos, anime MP4, troca de faixas, seek e retorno; depois abrir Diagnostico e
  confirmar NVTEGRA, descartes, bufferings e fila de audio. Se houver engasgo, enviar
  foto dessa tela, titulo e timestamp antes de alterar buffers ou filas.

## Auditoria corretiva da sessao em 29/08/2026 (0.8.1)

- A implementacao 0.8.0 foi auditada contra o contrato real do backend em
  `C:/iptv/src/routes/stream.js`, sem alterar esse repositorio. A renovacao R2 usa
  agora `POST /api/stream/session/:sessionId/refresh`, preservando a sessao e a
  fonte atuais. Upstream usa nova resolucao curta; uma queda de Wi-Fi nao encadeia
  mais refresh de 8 s com resolve de 20 s na mesma tentativa.
- `player_run` nao sobrescreve mais `PlayerRequest.userdata`. O contexto completo
  de `PlaybackSource` fica local ao ciclo de reproducao e e substituido de forma
  transacional somente depois que a API entrega uma URL valida. A URL deixou de
  usar buffer `static`, e o session id observado pelo heartbeat e atomico.
- Apenas falhas de abertura, leitura de faixas e rede (`-10`, `-2`, `-5`) entram
  em recuperacao. Falhas de codec, memoria ou renderer terminam com diagnostico em
  vez de repetir uma operacao incapaz de resolver o problema. Ha tres ciclos de
  pipeline, cada um com quatro renovacoes e espera progressiva; `B`/`-` cancela
  entre tentativas.
- Heartbeat (20 s) e progresso (15 s) continuam fora do decode, mas o player nao
  chama mais a API diretamente: usa callbacks e preserva o `userdata` do chamador.
  Pausa e seeks confirmados forcam salvamento de progresso. Chamadas periodicas e
  stop usam timeouts de 3/6 s para reduzir atraso ao sair.
- TLS foi realmente fechado nos dois caminhos. O AVIO libcurl direto usa
  `VERIFYPEER=1`/`VERIFYHOST=2`; a biblioteca FFmpeg HLS foi reconstruida com
  verificacao de CA, data e hostname no backend libnx. O patch local e aplicado por
  `tools/build_ffmpeg_https.sh`, que tambem valida os hashes das fontes oficiais.
- `tools/validate_release.ps1` verifica versoes, regressao de TLS, contrato de
  refresh/heartbeat/progresso, preservacao de userdata e simbolos HLS/HTTPS/NVTEGRA,
  alem de fazer build limpo e emitir tamanho/SHA-256 do NRO.
- Versao preparada: 0.8.1. Validacao local concluida com build limpo do aplicativo,
  sem erros ou avisos, e com `ff_https_protocol`, `ff_hls_demuxer`,
  `ff_h264_nvtegra_hwaccel` e `av_hwdevice_ctx_create` no ELF. A unica validacao que
  permanece obrigatoriamente no hardware e: filme e serie HLS por 10+ min, anime
  MP4, Wi-Fi desligado por 5/20 s, pausa longa, seek, audio/legenda e Diagnostico.
  Nao alterar buffers com base apenas em impressao; guardar titulo, timestamp e
  `player_stats.txt` se ainda houver engasgo.

## Paridade funcional com o site em 29/08/2026 (0.9.0)

- O cliente foi novamente comparado com `C:/iptv` sem editar o backend, que estava
  com mudancas do usuario. `tools/validate_site_contract.ps1` verifica os cinco
  catalogos, busca contextual e rotas de refresh/failover/heartbeat. As rotas
  publicadas foram sondadas sem credencial: health respondeu 200; search-v2,
  anime-home, dorama, refresh, fail e heartbeat responderam 401, confirmando que
  existem em producao e exigem autenticacao (nao sao contratos apenas locais).
- A busca antiga `/api/catalog/search` foi substituida por `search-v2`. Filtros
  agora separam Tudo, Filmes, Series, Animes e Doramas usando `search_scope`, como
  o site. A consulta usa percent-encoding de cada byte UTF-8; acentos, `&`, `#`,
  barras e outros caracteres nao podem mais truncar a URL.
- Anime consome `continueWatching`, `updatedToday`, `updatedWeek`, `popular`,
  favoritos, dublados, filmes e generos. O hero combina ate oito obras sem repetir
  ids, priorizando atualizados hoje e populares. `Filmes de anime` continua sendo
  tratado como serie porque o contrato real entrega essas obras em `series` com
  episodios; nao converter essa rail para filme.
- O detalhe de Serie/Anime/Dorama abre no primeiro episodio em andamento; sem um,
  escolhe o primeiro nao concluido. O backend envia `completed` como numero 0/1 e
  o cliente agora aceita numero ou booleano. Titulos enriquecidos (`ep_title`),
  percentual e barra de progresso aparecem na lista; as areas recebem cabecalho
  contextual NPLAY / SERIE, ANIME ou DORAMA.
- Ao existir progresso, um modal oferece A Continuar, X Comecar do inicio e B
  Cancelar; continuar e o padrao apos 6 s. Autoplay nao pula mais de forma brusca:
  ao final, o proximo episodio fica selecionado e, se a preferencia estiver ativa,
  aparece uma contagem de 5 s com A Assistir agora e B Ficar na lista.
- O supervisor agora espelha o failover do site de forma conservadora. Depois do
  reconnect nativo, quatro renovacoes e uma segunda falha da pipeline, chama uma
  unica vez `/api/stream/session/:id/fail`. Nao repete a mutacao em timeout para
  evitar penalizar varias fontes. Em sucesso tenta re-resolver o descritor completo,
  pois o endpoint de fail atual devolve URL/source_id mas nao delivery/container.
- Versao preparada: 0.9.0. `tools/validate_release.ps1` inclui o teste de contrato,
  build limpo, protecoes TLS e simbolos HLS/HTTPS/NVTEGRA. Build local passou sem
  erros ou avisos do aplicativo; NRO com 23.372.336 bytes e SHA-256
  `1c968164e7990b934974710c7e5c4dabbae172bdba9fe724c02222cc4fd43b4a`.
- Pendente obrigatorio no Switch: abrir um filme, serie, anime e dorama; pesquisar
  titulo acentuado e com simbolo; confirmar retomada/reinicio; episodio visto e em
  andamento; autoplay ligado/desligado; audio/legenda; HLS por 10+ min; queda de
  Wi-Fi e failover. Se falhar, registrar area, obra, episodio, timestamp, mensagem
  exata e Diagnostico/player_stats antes de alterar buffer ou fila de audio.

## Correcao de abertura e fluidez em 29/08/2026 (0.9.1)

- Teste no Switch da 0.9.0: filme R2 permanecia em Preparando/lendo faixas, serie
  chegava a tocar mas engasgava e anime MP4 encerrava a abertura com `End of file`.
  Os sintomas foram separados por transporte; nao tratar os tres como uma unica
  falha de servidor.
- `tools/probe_r2_packages.mjs` audita o banco/backend em modo somente leitura e
  nao imprime URL, token ou segredo. O teste real encontrou filme com 5 playlists,
  episodio com 6 e todos os primeiros objetos HTTP validos. O mesmo contrato abriu
  no ffprobe em 1,9-2,1 s. Trinta segundos foram lidos em 6,0 s no filme (5,0x) e
  9,0 s no episodio (3,3x). O MP4 de anime respondeu Range 206 com 512 KiB e tamanho
  total de 422.560.192 bytes. Assim, nesta amostra, R2/origem tinham vazao suficiente;
  a falha observada estava no caminho cliente do Switch.
- HLS agora limita `probesize` a 4 MiB, analise a 3 s de midia e 12 quadros de FPS.
  A abertura e a leitura de faixas possuem watchdogs de 35 s separados, evitando
  spinner indefinido. A UI diferencia playlist/fonte aberta de leitura de faixas.
- HLS ativa explicitamente conexoes persistentes e simultaneas para as playlists
  separadas de video/audio, alem de tres tentativas por segmento. Isso reduz novos
  handshakes TLS entre as renditions do pacote R2.
- MP4 remoto recebe `source_bytes` ja informado pela API, usa `Accept-Encoding:
  identity`, reconhece Content-Length/Content-Range atraves de redirecionamentos e
  nao interpreta resposta vazia inesperada como EOF. HTTP 416 exatamente no limite
  do arquivo e tratado como fim normal; resposta vazia incoerente termina em erro
  apos a janela de recuperacao, sem loop infinito.
- O decoder seleciona NVTEGRA explicitamente quando o formato e oferecido. Quadros
  transferidos como NV12 seguem direto por `SDL_UpdateNVTexture`, evitando swscale
  YUV420P em todos os frames; se o renderer do hardware recusar NV12, o fallback
  transacional recria IYUV e preserva o caminho antigo. Tentativas renovadas agora
  recebem o `PlaybackSource` completo atualizado, incluindo tamanho e entrega.
- `tools/validate_release.ps1` passou com build limpo, contrato site/cliente, TLS e
  simbolos HLS/HTTPS/NVTEGRA. NRO 0.9.1: 23.376.432 bytes, SHA-256
  `02cd0878cd3eaefecda992a09fe282a3667c1d1efe30ed0257fd95464049732c`.
- Pendente obrigatorio no hardware antes de afirmar resolucao final: filme R2 ate
  aparecer o primeiro quadro, serie por 10+ min, anime MP4, audio/legenda e tela
  Configuracoes > X Diagnostico. Confirmar `NVTEGRA ativo`; se houver engasgo,
  registrar obra/episodio, timestamp, mensagem exata e foto do diagnostico.

## Correcao de abertura sem bloqueio em 29/08/2026 (0.9.2)

- Teste real da 0.9.1 falhou no requisito principal: filmes e series R2 ficaram
  presos em `Preparando video / Lendo video e audio`, B nao respondia e animes
  MP4 continuaram sem abrir. Portanto a reproducao ainda nao deve ser considerada
  resolvida ate a 0.9.2 ser instalada e validada no Switch.
- A auditoria autenticada do R2 confirmou masters, child playlists, init/segmentos
  e MP4 por Range acessiveis. `ffprobe` no PC abre os HLS em cerca de dois segundos;
  a origem nao explica o bloqueio indefinido observado no console.
- Os masters R2 reais possuem `RESOLUTION`, mas nao `CODECS=`. O cliente agora
  aplica o contrato conhecido do empacotador (H.264/AAC/WebVTT), prioriza video e
  o primeiro audio e limita pacotes/duracao de probe para nao abrir 5-6 renditions
  antes do primeiro quadro. Faixas alternativas voltam a ficar disponiveis depois.
- Todo MP4 remoto, inclusive anime, passou do AVIO libcurl em blocos para HTTPS
  nativo do FFmpeg com Range/seek. Isso remove o caminho associado ao erro
  `abrir fonte: End of file`; arquivos locais continuam usando o protocolo local.
- A abertura e descoberta agora tem prazo de 20 s para HLS e 30 s para MP4. B ou
  menos interrompe ambas pelo callback do FFmpeg e retorna ao catalogo; falha no
  inicio faz no maximo uma renovacao, evitando prender o usuario em retries longos.
- Heartbeat e progresso permanecem suspensos ate codecs, decoders e saidas estarem
  prontos, eliminando concorrencia de API durante DNS/TLS/probe.
- Validacao local obrigatoria: build limpo, contrato estatico e auditoria R2. Teste
  pendente no hardware: filme R2, episodio R2 e anime MP4 por pelo menos 30 s,
  audio, seek e B durante cada etapa de preparacao. Se falhar, registrar exatamente
  o ultimo texto/etapa exibido; nao declarar a rodada concluida apenas pelo build.

## Transporte HLS libcurl e fallback real em 31/08/2026 (0.9.3)

- Teste real da 0.9.2 ainda nao reproduziu filmes nem series. A hipotese de apenas
  preencher codecs ausentes foi rejeitada; nao declarar reproducao resolvida sem
  confirmacao desta versao no Switch.
- Diferenca de transporte isolada: API/capas usam libcurl, mas HLS entregava master,
  child playlists, init e segmentos ao HTTPS interno do FFmpeg/libnx. O master
  abria e o bloqueio surgia nas conexoes aninhadas exibidas como leitura de audio
  e video.
- `AVFormatContext.io_open/io_close2` agora entrega cada recurso HTTP(S) aberto pelo
  demuxer HLS ao AVIO libcurl. Isso preserva o demuxer/decoder FFmpeg, mas remove TLS,
  redirects e Range do caminho libnx que travava. O perfil HLS usa ring de 2 MiB e
  blocos de 256 KiB por recurso, evitando multiplicar o ring de 16 MiB do MP4 pelas
  varias faixas abertas. Reuso HTTP interno foi desligado porque nao e compativel
  com AVIO customizado.
- Auditoria passou a enviar `Range: bytes=0-262143`, `Accept-Encoding: identity` e
  User-Agent do Switch tambem para manifests. Em amostra conservadora de 100 fontes
  ativas, 92 estavam validas e oito retornaram 404 real; filme e episodio validos
  leram 30 s a 17,0x e 5,2x, respectivamente. Anime MP4 respondeu Range 206.
- Foi encontrada uma regressao independente na 0.9.2: o limite de uma recuperacao
  encerrava o player antes do ramo de fonte alternativa (`retry_count == 1`). O
  inicio agora permite renovar a mesma sessao e depois chamar `/fail` para trocar
  a fonte. Isso e essencial para os oito ponteiros R2 404 ainda publicados.
- `tools/probe_r2_packages.mjs` verifica por padrao os 50 filmes e 50 episodios mais
  recentes; `--full` percorre todas as 3.189 referencias. Timeout de rede e reportado
  como nao confirmado, separado de 404, para nao desativar conteudo por saturacao.
- Pendente obrigatorio no hardware: filme e serie por 30+ s, audio presente, seek,
  B durante preparacao e uma obra cuja primeira fonte falhe para confirmar fallback.
  Se houver falha, fotografar o ultimo texto exato e Diagnostico do player.

## Cadeia CA e bootstrap seguro em 31/08/2026 (0.9.4)

- A 0.9.3 foi publicada, mas o console instalado nao conseguiu consulta-la:
  `SSL peer certificate or SSH remote key was not OK (-60)`. A falha ocorre antes
  do download e nao tem relacao com a troca atomica do NRO.
- Causa confirmada no toolchain: libcurl 7.69.1/mbedTLS do port Switch tem
  `curl-config --ca` vazio. O cliente ativava `CURLOPT_SSL_VERIFYPEER=1` e
  `CURLOPT_SSL_VERIFYHOST=2`, mas nao fornecia `CURLOPT_CAINFO`; a confianca TLS
  dependia de um armazenamento que nao e garantido no homebrew.
- `data/cacert.bin` contem o bundle Mozilla publicado pelo curl em 13/08/2026,
  188.900 bytes, 121 CAs, SHA-256
  `f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9`.
  O checksum oficial e exigido por `tools/validate_release.ps1`.
- No boot, `net_init` compara o arquivo embutido com
  `sdmc:/switch/.nplay-ca.pem`; se ausente/divergente, grava `.new` e ativa por
  rename. Todos os easy handles recebem esse caminho por `net_configure_curl`,
  cobrindo API, GitHub updater, downloads e o AVIO HLS da 0.9.3. Verificacao de
  peer e hostname permanece ligada; nao foi criado fallback TLS inseguro.
- Erro -60 agora mostra orientacao para conferir data/hora do console, pois um
  relogio incorreto ainda invalida certificados mesmo com a CA correta.
- Bootstrap inevitavel: 0.9.2/0.9.3 ja instaladas nao possuem o bundle e nao podem
  adquirir esta correcao se o GitHub continuar recusado. Primeiro testar sincronizar
  data/hora do Switch. Se persistir, copiar o Nplay.nro 0.9.4 manualmente uma unica
  vez (microSD/FTP/USB); atualizacoes seguintes voltam a funcionar pelo aplicativo.
- Pendente no hardware apos o bootstrap: buscar update no GitHub, login/catalogo,
  filme/serie HLS por 30+ s, anime MP4 e confirmar criacao de `.nplay-ca.pem`.

## Catalogo assincrono e isolamento do HLS em 31/08/2026 (0.9.5)

- Teste real da 0.9.4 confirmou que a cadeia CA corrigiu a atualizacao e que o
  catalogo/animes abrem. Permaneceram tres falhas: filme HLS fechava o software,
  Series falhava ao sincronizar e cada troca de aba congelava por 15-20 segundos.
- A causa objetiva da navegacao lenta era `load_landing` chamar `api_get` de forma
  sincrona na thread que desenha e le o controle. Inicio, Filmes, Series, Animes e
  Doramas agora carregam em uma thread dedicada, com cache independente por aba.
  A troca visual e imediata; a primeira visita mostra carregamento responsivo e as
  seguintes reutilizam o payload. Series recebe ate 30 s sem bloquear a interface.
- A troca repetida entre abas podia reconstruir `_switchHeroes` dentro do mesmo JSON
  em cache. O array auxiliar anterior agora e removido antes da reconstrucao, evitando
  duplicatas e crescimento de memoria em navegacao prolongada.
- A diferenca do crash continua isolada ao transporte HLS: animes MP4 funcionam,
  enquanto filmes/series usam varias conexoes libcurl simultaneas para master,
  renditions e segmentos. Esses handles longos deixaram de entrar no `CURLSH` global
  do libcurl 7.69; mantem CA e keepalive, mas possuem cache de conexao/DNS/TLS proprio.
- Cada abertura grava atomicamente a ultima etapa em
  `sdmc:/switch/.nplay-player-boot.txt`, de `01 inicio` ate `09 reproduzindo`.
  Configuracoes > X Diagnostico exibe essa etapa mesmo se o processo foi encerrado
  pelo sistema antes de gerar `player_stats.txt`.
- Pendente obrigatorio no hardware: alternar rapidamente Filmes/Series/Animes,
  confirmar que B continua responsivo durante carga, abrir filme e serie por 30 s.
  Se ainda fechar, reiniciar, abrir Configuracoes > X e fotografar `Ultima etapa`;
  esse marcador passa a localizar o crash sem depender de suposicao.
