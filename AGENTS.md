# Continuidade para agentes

## Integracao 0.12.49 — 04/10/2026

- Checkout ativo: `C:/NplaySwitch/.codex-tmp/switch-access-expiry`, branch
  `codex/switch-player-hardening`. Integra ba7d46d e 2b8d0d0 sobre ca5a7a8;
  as duas branches 0.12.48 eram paralelas, nao substituir uma pela outra.
- Letreiros posicionados mantem coordenadas do video e ficam fora das falas.
  Dedup exige texto+posicao+ancoras. Placa e fala iguais nao se apagam.
- Fila continua limitada a 32 cues, com ate oito letreiros; letreiros nunca
  expulsam falas. Extensao fora de ordem mantem max_short atualizado.
- Regressoes novas obrigatorias: test_positioned_subtitles (11 casos, rajada
  de 10 mil), geometria extraida de draw_signs (6 casos) e side data real
  WebVTT na rendition HLS/VTT direto. Fixtures CLI usam FFmpeg 8.1.1;
  SDK/NRO usa 7.1. Nao confundir esses testes com GPU/console reais.
- Build completo ARM64 -Werror e validador completo sem SkipMediaFixtures
  passaram; NRO 24241999 bytes, SHA-256 registrado no relatorio.
  Nao publicar/promover como validado
  no console antes do teste fisico. Sem deploy/reprocessamento do backend.
- Relatorio e comandos: docs/PLAYER_INTEGRATION_0_12_49.md. Pacotes antigos
  sem coordenadas nao ganham posicao apenas atualizando o cliente.

## Letreiros posicionados — 0.12.48 (04/10/2026)

- Placas/onomatopeias do fansub caiam embaixo misturadas com as falas: o
  backend usava `-c:s webvtt`, que perde `\pos/\an`. Backend (branch
  `claude/nplay-tv-switch-review-n4drzv` do Nplay) converte ASS -> WebVTT com
  `position:X% line:Y% align:A`; web/TV e Switch desenham no lugar.
- NAO emitir `line:..%,end` nem `position:..%,center`: o Chromium descarta a
  configuracao inteira. So `line:` em % marca letreiro (hls.js poe position=50
  em cue comum).
- Switch le `AV_PKT_DATA_WEBVTT_SETTINGS`; `SubtitleStoreCue` tem 20 bytes;
  falas e letreiros saem separados (`subtitle_store_signs`, `draw_signs`).
- Pacotes R2 antigos so ganham posicao com a legenda refeita; sem lote em
  massa. Ver `docs/SUBTITLE_SIGNS_0_12_48.md`. Hardware nao testado.

## Rodada 0.12.14 (25/09/2026)

- Auditoria adicional do ciclo Home/Continuar, Historico, detalhe e Biblioteca.
  O EOF sem duracao HLS agora marca a obra como vista pelo endpoint existente;
  EOF antes de mostrar um quadro e erro, nao avanca para outro episodio.
- A selecao da Biblioteca sobrevive a reordenacao dos jobs por polling pelo
  identificador da obra e do episodio. Consulta de serie cancelada ou que nao
  iniciou nao conserva autoavanco pendente.
- Build ARM64 limpo, teste nativo de ordem/reordenacao, 37 contratos HTTP e
  integracao de playback/Continue do backend passaram. A suite geral do backend
  continua bloqueada por teste antigo que espera timer Ter/Sex enquanto a
  configuracao atual agenda diariamente; testes TV legados tambem estao
  desatualizados. Ver auditoria. NRO nao foi executado em hardware nesta rodada.

## Rodada 0.12.13 (25/09/2026)

- EOF de episodio iniciado por Home/Continuar ou Historico agora conserva o
  contexto da serie e oferece o proximo episodio, como ja ocorria no detalhe.
  A escolha cobre a virada de temporada e series irmas de `season_group`.
- A consulta de detalhe necessaria apos EOF usa `CatalogFetch` cancelavel.
  Autoplay desligado apenas seleciona o proximo episodio; B cancela a contagem.
- Biblioteca de episodios preparados e ordenada por T/E. O auto-avanco exige
  coordenadas contiguas e job pronto, com a mesma contagem de 5 s. A consulta
  de marcas de visto agora e assincrona e isolada por perfil.
- HLS com duracao desconhecida envia progresso ao backend. Toque nos dialogos
  de retomada e proximo episodio funciona. Ver
  `docs/EPISODE_CONTINUATION_AUDIT_2026_09_25.md`.
- Build e teste local de ordem validam codigo, nao a apresentacao no Switch.

## Rodada 0.12.11 (24/09/2026)

- O detalhe de serie agora escolhe o episodio parcialmente assistido mais
  recente por `progress_updated_at`, em vez do primeiro na lista.
- O fallback do Switch consulta `/api/stream/:itemId/variants` antes de usar
  `/fail`; sem alternativa reproduzivel, nao provoca a desativacao global da
  unica fonte. Depois de `/fail`, aceita somente o descriptor completo obtido
  em nova resolucao, com o `source_id` esperado; nao herda container antigo.
- Heartbeat/progresso do player so comecam depois do primeiro quadro realmente
  apresentado. Uma tentativa que nao mostrou video nao regrava o progresso
  salvo nem aciona trabalho de prefetch pelo ponto antigo.
- Risco de backend ainda aberto: `/stream/session/:id/fail` desativa a fonte
  globalmente apos falha de um cliente, mesmo se a causa for local. A rota
  deveria escolher alternativa por sessao e devolver descriptor completo.
  Nao ampliar chamadas a ela ate coordenar mudanca no servidor/site.

## Rodada 0.12.10 (24/09/2026)

- A retomada continua ativa: o ponto salvo e buscado no HLS/R2. A 0.12.9
  apenas iniciava desde zero quando a tentativa de seek nao exibia quadro.
- Em X-Men '97 T1E1, sondagens de leitura do R2 nos segundos 48 e 600
  entregaram os segmentos, mas havia cerca de 90 pacotes de video de preroll
  antes de cada quadro alvo. FFmpeg 7.1 usa a faixa indicada para o seek HLS e depende do primeiro
  timestamp lido para mapear o segmento. O cliente agora le um pacote para
  estabelecer essa linha do tempo e busca na faixa de video explicitamente.
- Durante o preroll, o audio nao toca antes do ponto salvo; quadros anteriores
  sao decodificados sem contar como atrasados. O relogio de apresentacao comeca
  no primeiro quadro no ponto alvo, em vez de correr enquanto a rede carrega.
- Validar no console ainda e necessario para concluir se o hardware apresenta
  e sincroniza a retomada. Os testes locais confirmam build e acesso/decodificacao
  do R2, mas nao executam o NRO em um Switch.

## Rodada 0.12.9 (24/09/2026)

- Relato no hardware: começar do início reproduziu sem travar; a espera em
  “Preparando vídeo” parece concentrada no fluxo de Continuar. O diagnóstico
  da reprodução bem-sucedida registrou início em cerca de 3,7 s e zero pausas.
- A retomada HLS busca a posição antes do primeiro quadro. Se não houver quadro
  em até 12 s após o seek, a tentativa volta ao início na mesma fonte. Nenhum
  progresso é substituído pela posição de uma tentativa sem quadro exibido.
- Um HLS que atinge EOF após o seek sem mostrar quadro também ativa a
  recuperação. A causa exata do bloqueio no FFmpeg/console ainda não foi
  comprovada por rastreio dessa tentativa específica.

## Rodada 0.12.8 (24/09/2026)

- Logs reais de X-Men '97 T1E1 na 0.12.7 mostram entrega R2/HLS, playlists
  HTTP 200 e abertura em 1,56 s em uma tentativa. As duas tentativas preservadas
  terminaram por B antes do primeiro quadro; portanto nao comprovam a causa da
  espera longa relatada. O trace da tentativa longa foi substituido por novas
  tentativas. Nao afirmar que a reproducao foi validada no Switch.
- A tentativa com retomada aos 48 s escreveu 76 eventos `hls-io` antes de exibir
  qualquer quadro. A 0.12.8 remove gravacoes normais por recurso do caminho de
  abertura, preserva erros e registra progresso a cada 5 s enquanto espera.
- `AVSEEK_SIZE` nao aguarda mais ate 15 s quando o tamanho do segmento ainda e
  desconhecido; retorna ENOSYS imediatamente, como fazia ao fim da espera.
- Configuracoes > X permite subir/descer pelas paginas do trace no proprio
  console. O arquivo continua em `sdmc:/switch/.nplay-player-trace.log`.
- Build e contratos locais validam o NRO, mas fluidez e abertura no hardware
  continuam sem confirmacao direta neste ambiente.

## Objetivo atual

O foco e otimizar o homebrew Nplay para Nintendo Switch sem trocar a arquitetura SDL2/FFmpeg/libcurl existente. Priorize fluidez da UI, uso previsivel de memoria e estabilidade do streaming.

## Rodada 0.12.7 (24/09/2026)

- No Switch 0.12.6, o usuario relatou que B durante "Preparando video" fez a
  reproducao iniciar. O cancelamento era marcado mas o callback voltava a
  aceitar leituras quando B era solto; FFmpeg podia recuperar a faixa e seguir.
  O estado cancelado agora e permanente na tentativa e conferido apos root,
  abertura, probe e cada leitura. O prazo HLS tambem e absoluto ate o primeiro
  quadro e nao pode ser reiniciado por uma etapa posterior.
- Captura 0.12.6: 1920x1080, 1185 quadros, dois descartados, quatro pausas
  atribuidas a leitura (pior 661 ms), tres leituras >=250 ms (pior 631 ms),
  fila de audio maxima 192 ms e inicio em 8557 ms (6694 ms na abertura).
- A thread de video fazia varias gravacoes sincronas no cartao SD dentro de
  `av_read_frame` ao abrir/fechar cada recurso HLS. Depois do primeiro quadro,
  eventos normais de segmento deixam de ser gravados; erros e esperas >=1 s
  continuam, e resumos de rede e prefetch sao gravados ao sair. Ainda falta medir no
  hardware se isso reduz os engasgos e se persistem demoras do R2.

## Rodada 0.12.6 (24/09/2026)

- Captura real da 0.12.5: um titulo chegou a reproduzir por cerca de 75 s com
  oito intervalos >=250 ms entre quadros, seis leituras lentas (max. 543 ms),
  um quadro descartado e nenhum erro. A causa das pausas ainda nao foi isolada.
- X-Men '97 permaneceu em "Preparando video" e B nao saiu. O callback HLS agora
  consulta B/Minus durante transferencias de playlist e esperas do AVIO sem
  consultar SDL a cada leitura que ja possui bytes. O cancelamento limpa o
  evento de botao pendente ao voltar para o catalogo.
- O pool de handles libcurl passou a atender playlists HLS sequenciais alem dos
  segmentos. A tela de diagnostico persiste tempo de abertura, probe e primeiro
  quadro, e divide pausas por leitura, sincronizacao e outro trabalho.
- Build/contrato local passaram; falta validar no Switch real dois episodios de
  X-Men '97, cancelamento com B e fluidez por pelo menos dez minutos. Se o
  preparo persistir, usar Configuracoes > X apos reiniciar sem tocar outro video.

## Rodada 0.12.5 (24/09/2026)

- Relato no hardware 0.12.4: varios episodios de X-Men '97 permanecem na tela
  "Preparando video". A foto dos ajustes repete estatisticas antigas do ultimo
  video iniciado; nao prova que a tentativa atual decodificou qualquer quadro.
- O AVIO customizado esperava 20 s pelo primeiro byte, devolvia EAGAIN e podia
  repetir esse ciclo sem limite. Agora cada recurso sem primeiro byte termina
  com ETIMEDOUT, e a tentativa HLS tem limite absoluto ate o primeiro quadro.
- O diagnostico na tela remove o prefixo de heap/proc que cortava o codigo HTTP
  e o evento. O log integral na microSD permanece igual.
- Ainda falta trace de um episodio especifico e teste no Switch real para
  distinguir pacote R2 ausente/lento, manifesto problematico e falha do cliente.

## Rodada 0.12.4 (24/09/2026)

- Captura real da 0.12.3: 17 pausas, 24 leituras HLS lentas, maxima de 5295 ms,
  so tres quadros descartados. A hipotese principal e latencia de rede/segmento,
  nao falta de decodificacao. Falta comparar com o trace detalhado no Switch.
- HLS passa a descartar playlists de audio/legenda nao selecionadas; trocar faixa
  atualiza o descarte. Segmentos R2 usam pre-abertura do proximo segmento do
  FFmpeg, com buffers libcurl independentes e limite de memoria existente.
- O banner nao cobre mais o cabecalho ao rolar; relacionados usam cards maiores;
  catalogo refaz a aba ativa ao sair do player; preparacao usa arte da pipoquinha.
- Contrato site/Switch e compilacao ARM64 limpa passaram. A mudanca de fluidez,
  memoria durante longas sessoes, troca de faixas e desenho ainda precisa de
  confirmacao no hardware, conforme `docs/SWITCH_0_12_HARDWARE_CHECK.md`.

## Rodada visual 0.12.0 (24/09/2026)

- A 0.11.0 publicada manteve o layout antigo. A 0.12.0 em desenvolvimento porta
  a composicao visual de `public/tv/shell.css` para o SDL: header de 95 px,
  paleta TV, marca embutida, destaque panoramico, cinco cards por fileira,
  foco branco, detalhe largo e episodios com miniaturas.
- Build e contrato site/Switch foram executados localmente. Ainda falta testar
  desenho e navegacao em Switch real. O 404 ao pressionar Assistir na 0.11.0
  nao foi diagnosticado; nao tratar o build como prova de reproducao.
- Os novos assets visuais nao podem aumentar o numero de texturas de catalogo
  carregadas durante abertura HLS. Manter o teto LRU e liberar memoria para o
  player antes de tocar.

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

## Legendas de remux em fluxo — 0.12.38 (01/10/2026)

- A sessao textual retornada por `/api/stream/hot` e agora preservada em
  `PlaybackSource.hot_session_id`. Ela **nao** substitui `PlaybackSource.session_id`,
  que continua sendo somente o inteiro da sessao de telas/heartbeat.
- Quando o remux fMP4 nao anuncia `AVStream` de legenda, o Switch consulta
  `/api/stream/hot/:sid/probe`; as faixas retornadas entram no painel como
  WebVTT externos. Ao selecionar uma, `/subtitles/:index.vtt` e consumido em
  fluxo, pois o backend envia `WEBVTT` cedo e mantem a resposta aberta enquanto
  extrai os cues. Isso evita bloquear a reproducao esperando a extracao acabar.
- O transporte novo aceita exclusivamente HTTPS, valida TLS e hostname, nao segue
  redirecionamentos, limita 4 MiB mesmo sem `Content-Length`, nao persiste URL/SID
  em diagnostico e tem cancelamento proprio. B/toque encerra apenas a legenda;
  jamais reutilizar `demux_abort`, que tambem derrubaria video e audio.
- O resultado so substitui a faixa anterior depois que o cabecalho WebVTT foi
  validado. 404, 503, VTT invalido, timeout, cancelamento ou resposta tardia
  conservam a selecao anterior. Cues sao protegidos por mutex e a thread e sempre
  cancelada/joinada antes de trocar episodio ou sair do player.
- Validacao local 01/10/2026: NRO ARM64 compilado com `-Werror`; simulacoes de
  cancelamento, join, 404/503, resposta tardia, teto de bytes, WebVTT fragmentado
  (BOM/CRLF/keepalive/EOF), HLS/seek e WebVTT direto passaram. Pendente obrigatorio
  em hardware: abrir um remux com legenda, trocar/desligar faixa, seek durante
  extracao e trocar episodio sem esperar o fim da extracao. Nao alegar paridade
  total com navegador sem essa confirmacao.

## Rodada 0.12.18 em 28/09/2026

- Integracao foi feita sobre `origin/codex/switch-rebuild` 0.12.17, sem substituir
  o HUD e as correcoes de estabilidade mais novas pelos arquivos antigos da linha
  0.11. Apenas as melhorias ainda ausentes do commit `453d1bf` foram portadas.
- Idiomas de audio/legenda sao normalizados; a preferencia da conta e carregada
  apos o primeiro catalogo e a faixa ativa e preservada em recuperacoes e entre
  episodios quando a fonte nao possui tag de idioma.
- O player descarta faixas HLS nao usadas e, ao trocar audio/legenda, ativa apenas
  a nova faixa. Audio antigo anterior ao ponto atual e ignorado por ate 5 s.
- Esperas de sincronismo de video agora ocorrem em fatias de 8 ms e cedem quando
  existe comando do Joy-Con. O pool de conexoes HLS e limpo ao encerrar o player.
- ZL/ZR percorre todas as versoes de audio da serie. L/R entre temporadas agrupadas
  tenta conservar a versao atual e informa quando ela nao estiver disponivel.
- Versao preparada: 0.12.18. Validar no Switch real filme, episodio de serie,
  anime e dorama; testar troca de audio/legenda, episodio seguinte e temporada
  agrupada antes de considerar o comportamento de hardware definitivamente aprovado.

## Correcao 0.12.19 em 28/09/2026

- Teste do usuario mostrou que a 0.12.18 mantinha o HUD anterior e selecionava
  ingles mesmo com a conta configurada para Dublado.
- Causa do idioma: o arquivo local criado por versoes antigas ganhava da
  preferencia atual da conta. A ordem foi corrigida para a conta escolher
  portugues/estrangeiro primeiro; escolha local e `audio_hint` sao fallback.
- A pausa agora tem um painel visual amplo no estilo do PC e a barra inferior
  mostra idioma de audio e estado da legenda, produzindo uma mudanca visivel sem
  substituir o pipeline 0.12.x pelo `player.c` antigo de `f5296a1`.
- Versao preparada: 0.12.19. Pendente no hardware: confirmar Dublado em fonte
  multiaudio, Legendado com legenda PT, troca manual e captura do HUD pausado.

## Auditoria profunda do player 0.12.20 em 28/09/2026

- A 0.12.19 nao continha o HUD modular de `f5296a1`; apenas aproximava o layout
  antigo. `player_ui.c/.h` foi portado sobre o pipeline 0.12.x, preservando HLS,
  NVTEGRA, limites de memoria, diagnostico e recuperacao de sessao.
- Pausa, loader, buffering, timeline e painel de audio/legenda usam agora o mesmo
  sistema visual. O painel tem duas colunas e a busca nao desenha mais o modal
  antigo por cima da timeline moderna. Area de toque foi alinhada ao novo HUD.
- Causa adicional do ingles: pacotes R2 antigos usam `eng` na primeira faixa e
  `und` na segunda dublada. `audio_policy.c` replica o contrato vigente do site,
  cobre PT explicito, regra legada, original/Legendado e evita comentario.
- Serie passa a priorizar a versao Dublada/Legendada aberta. Recuperacao e
  episodio seguinte preservam o idioma, nao apenas a posicao da faixa. Arquivos
  locais de audio/legenda agora sao separados por perfil.
- Metadados estaticos do HUD sao montados uma vez por reproducao; nao enumerar
  streams nem formatar todas as faixas por quadro. O cartao de proximo episodio
  fica oculto ate possuir foco/confirmacao realmente integrados.
- Detalhes, matriz de decisao e roteiro de hardware estao em
  `docs/PLAYER_AUDIO_UI_AUDIT_0_12_20.md`.
- Suite completa passou antes do bump: build limpo sem warnings, contratos de
  site/player/API/episodios/remux, teste de audio com `-Werror`, TLS e simbolos.
  Ainda e obrigatorio testar em hardware filme R2 legado, Legendado, dois
  episodios, troca manual, pausa, painel, timeline e queda de rede.

## Pipoca, troca de faixas e contexto 0.12.21 em 28/09/2026

- O loader modular voltou a desenhar o atlas animado da pipoca. O anel ficou
  pequeno e secundario; nao substituir novamente a identidade por uma letra.
- A troca de audio HLS agora ativa a rendition e faz um unico seek para tras no
  ponto atual. O callback de interrupcao anima `Trocando audio` durante I/O e B
  continua cancelando. Eventos `audio-switch-*` medem o tempo real no hardware.
- A ativacao de legenda HLS usa o mesmo realinhamento. Streams de legenda R2 que
  ficaram com codec `NONE` por causa do probe curto sao tratados como WebVTT,
  conforme o contrato do empacotador, e deixam de desaparecer do painel.
- Opcoes repetidas de legenda recebem indice visivel. O Switch continua limitado
  a 16 faixas de audio e 16 de legenda para manter uso previsivel de memoria.
- Episodios abertos pela Home/Historico levam seu JSON como pista. Se o detalhe
  da serie nao estiver carregado, o HUD ainda usa serie, T/E, titulo e sinopse;
  `/stream` fornece T/E como ultimo fallback. `g_ser` so e reutilizado quando o
  `series_id` corresponde, evitando idioma/metadado herdado de outra serie.
- Validacao local: suite limpa 0.12.21 sem erros/avisos, contratos de site/API,
  relogio, audio, episodios, remux e simbolos aprovados. No Switch, testar troca
  de audio no meio de R2 e um master com
  varias legendas; a maquina local nao substitui essa confirmacao de hardware.

## Legendas temporizadas e rollback de faixas 0.12.22 em 28/09/2026

- `source/subtitle_queue.c` introduz uma fila limitada de oito cues. Legendas
  futuras nao aparecem antes da hora, cues sobrepostos podem coexistir e itens
  vencidos sao removidos sem crescimento continuo de memoria.
- O tempo passa a respeitar `start_display_time` e `end_display_time`; ausencia de
  duracao recebe fallback de quatro segundos. Quebras `ASS \\N` sao preservadas.
- O renderer antigo duplicado foi removido. Legendas passam exclusivamente pelo
  `PlayerHud`, inclusive com HUD oculto, pausado, buffering, timeline e painel de
  faixas. O cache de quebra de texto agora invalida pelo conteudo, nao pelo endereco
  reutilizado do buffer, evitando manter uma fala anterior na tela.
- Trocas de audio e legenda agora abrem um decoder candidato. O decoder atual so e
  descartado depois que a sincronizacao HLS confirma sucesso; falha de abertura ou
  seek restaura os streams anteriores e informa rollback ao usuario.
- `tools/test_subtitle_queue.c` cobre cue futuro, sobreposicao, expiracao, duracao
  ausente e reset. A validacao de release executa o teste automaticamente.
- Pendente no hardware: conferir WebVTT e ASS reais, duas falas simultaneas, seek
  durante legenda, ativar/desativar faixa e forcar falha de uma rendition HLS para
  confirmar que audio/legenda anteriores continuam funcionando.

## Seek, cancelamento e metricas de audio 0.12.23 em 28/09/2026

- Seek usa primeiro `avformat_seek_file` com uma janela de 15 segundos, que leva
  todos os streams ativos em conta, e preserva `av_seek_frame` como fallback para
  fontes/demuxers que nao implementam a API mais completa.
- Trocas HLS de audio e legenda possuem operacao propria com limite de dez segundos.
  `B` durante essa espera cancela somente a troca, consome o evento e bloqueia nova
  saida enquanto o botao continuar segurado; o player anterior permanece ativo.
- Timeout, cancelamento, abertura ou seek malsucedido restauram disposicoes e
  decoders anteriores. O diagnostico diferencia cancelamento (`op=1`) e timeout
  (`op=2`) nos eventos `audio/subtitle-switch-*`.
- `player_stats.txt` registra faltas reais da fila SDL depois de ela ter sido
  abastecida, travessias acima de 1,5 segundo, falhas de troca e maior tempo de
  troca. Configuracoes mostra esses quatro numeros no diagnostico do player.
- Nao foi imposto um teto cego a `SDL_QueueAudio`: descartar PCM ou dormir dentro
  do loop atual pode criar buracos e atrasar video. Medir no Switch vem antes da
  futura fila PCM limitada/thread de audio.
- Pendente no hardware: cancelar troca segurando B, deixar uma rendition exceder
  dez segundos, seek em MP4/HLS, foto do diagnostico depois de uma reproducao
  fluida e outra com engasgos, e verificar que faltas/filas altas correspondem ao
  comportamento ouvido.

## Prioridade PT-BR corrigida em 0.12.24 em 28/09/2026

- Causa confirmada do ingles persistente: `audio_policy_choose` aplicava a pista
  do episodio anterior antes de `audio_pref`. Assim, continuidade `en` vencia uma
  faixa `pt-BR` explicita mesmo com a conta/versao em Dublado.
- `audio_hint_priority` separa duas origens antes indistinguiveis. Somente retry,
  refresh ou fallback da mesma reproducao recebe prioridade e conserva uma troca
  manual. Ao abrir outro episodio/conteudo, Dublado procura PT-BR (ou a regra
  legada `eng + und`) e Legendado procura original antes de considerar a pista.
- Em `Tanto faz`, continuidade e escolha local continuam sendo respeitadas. Se a
  preferencia explicita nao existir naquela fonte, a continuidade vira fallback
  antes do default do manifesto.
- O evento `streams selected` inclui agora preferencia, pista, prioridade e mapa
  compacto das faixas (`1:en*,2:pt`). Isso permite provar pela foto/trace se um
  pacote chegou rotulado incorretamente, sem registrar URLs ou credenciais.
- `test_audio_policy` cobre PT versus continuidade inglesa tanto na abertura de
  outro episodio quanto na recuperacao da mesma reproducao. Pendente no hardware:
  filme dublado, dois episodios em sequencia, troca manual seguida de queda de rede
  e captura do diagnostico se o mapa nao mostrar uma faixa `pt`.

## Player resiliente em 0.12.25 em 28/09/2026

- Causa estrutural dos controles congelados: `av_read_frame` fazia rede e demux
  na mesma thread que processava SDL. Playlist/segmento lento impedia pausa,
  retorno e HUD. A leitura agora roda em worker proprio e entrega pacotes por
  fila limitada a 32 itens/4 MB; pausar nao causa consumo indefinido de memoria.
- Seek e troca de audio/legenda nao mutam mais um demuxer HLS ativo. A tentativa
  atual fecha de modo controlado e a mesma fonte reabre na posicao atual. Isso
  elimina o falso rollback que restaurava decoders, mas deixava playlists em
  estado interno invalido depois de seek interrompido.
- Audio manual recebe prioridade de indice exato durante a reabertura, inclusive
  quando duas faixas possuem o mesmo idioma. Legenda preserva faixa exata ou o
  estado desligado, sem ser substituida pela preferencia salva.
- O manifesto raiz ja baixado e inspecionado sem nova requisicao. Se anuncia
  `EXT-X-MEDIA:TYPE=SUBTITLES` mas o FFmpeg ainda nao criou as AVStreams, o probe
  completo e habilitado. Se as faixas ja existem, a abertura rapida e mantida.
- Latencia da thread de demux nao altera mais `wall_start`: a leitura e paralela
  e soma-la ao relogio atrasava o video a cada segmento lento. Ao sair, o player
  sinaliza o callback de interrupcao antes de aguardar o worker.
- Testes novos cobrem o parser de renditions do master HLS e escolha manual exata
  entre faixas do mesmo idioma. Build e contratos locais devem passar antes da
  release; teste de comportamento ainda depende do Switch real.
- Pendente no hardware: pausa por 30/120 s, retomada, L/R/ZL/ZR, timeline, todas
  as faixas de audio e legenda ligada/desligada em filme e episodio. Em falha,
  fotografar o diagnostico e informar titulo/tempo; procurar no trace
  `demux worker-start`, `controlled-restart` e `hls-master renditions`.

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

## Documento de investigacao do playback em 31/08/2026

- Novo relato real da 0.9.5: anime ainda pode reproduzir, mas filmes fecham o
  processo, Series pode falhar ao carregar e respostas HTTP 502 aparecem durante
  a abertura. A latencia percebida continua alta em varias transicoes.
- O fluxo completo foi documentado em
  `docs/Nplay_Switch_Fluxo_Reproducao_e_Plano_de_Investigacao.docx`. O gerador
  reproduzivel fica em `tools/build_playback_investigation_doc.py`.
- Conclusao para o proximo agente: nao tratar 502 e crash como a mesma falha.
  `/api/play/:itemId` devolve 502 quando `resolveStreamUrl` nao renova nenhuma
  fonte; o fechamento nativo ocorre depois, dentro de HLS/AVIO/FFmpeg/decoder.
- Permanecem sincronas na thread da interface: detalhe de filme, detalhe de serie
  e consulta de progresso antes do player. A landing de abas ja e assincrona, mas
  isso nao elimina esses bloqueios nem a latencia real do backend.
- Proxima rodada deve ser de instrumentacao, nao outra mudanca ampla: correlacao
  item/source/session, status e duracao de `/stream` e `/play`, ultima etapa,
  quantidade/bytes de AVIO e memoria livre. Depois, comparar A/B HTTP nativo do
  FFmpeg contra AVIO libcurl usando o mesmo manifesto R2.
- O DOCX passou auditoria estrutural, de estilos, tabelas e acessibilidade (zero
  alertas). Nao houve QA visual por PNG porque LibreOffice/`soffice` nao esta
  instalado neste host; isso deve ser feito por um agente/host que o possua.

## Correcao de ciclo de vida e memoria HLS em 01/09/2026 (0.9.7)

- A tentativa 0.9.6 adicionou uma segunda recuperacao dentro de
  `player_play_internal` por `goto seamless_reopen`, embora `player_run` ja seja
  o supervisor de sessao. Ela renovava um descritor diferente do ativo e mantinha
  textura/estado entre pipelines desmontadas. Essa duplicidade foi removida; toda
  reabertura volta a ser transacional e ocorre somente em `player_run`.
- `source/curl_avio.c` nao usa mais um ring de 2 MB para todo recurso HLS.
  Manifestos/legendas usam bloco de 64 KB + ring de 256 KB; segmentos usam bloco
  de 256 KB + ring de 1 MB. Em uma master com varias renditions isso reduz varios
  megabytes de heap simultanea sem reduzir o bloco de midia usado para throughput.
- URLs aninhadas deixaram de ser truncadas silenciosamente em 2.048 bytes. O AVIO
  agora conserva a URL completa em alocacao do tamanho exato.
- Se um host ignora `Range` ou devolve mais dados que o bloco pedido, o callback
  marca overflow e rejeita o recurso. A versao anterior aceitava o prefixo
  truncado e podia entregar manifesto/fMP4 corrompido ao demuxer.
- O diagnostico persistente registra modo `application`/`applet` e, durante HLS,
  numero de recursos AVIO e KB reservados, sem gravar URL assinada ou credencial.
  Se houver novo fechamento, abrir Configuracoes > X imediatamente e fotografar
  `Ultima etapa`; exemplos esperados: `03 HLS ativo: 3 recursos, 4032 KB` ou
  `03 HLS sem memoria`.
- `tools/validate_release.ps1` impede regressao dos tetos HLS, aceita apenas bloco
  completo, exige diagnostico e proibe o `goto`/renew duplicado. Build limpo 0.9.7
  passou sem warnings: `Nplay.nro` 23.573.040 bytes, SHA-256
  `1fe4ae6dc9c8a1b683e68b8f3baa646871295abedec3f74c462b0ab086ae1b29`.
- O contrato integrado do backend (`scripts/player-flow-contract-test.mjs`) passou
  com R2 gerenciado, refresh e fallback. Auditoria somente leitura do banco local
  encontrou 3.287 assets R2 prontos, 3.272 fontes R2 ativas e 15 episodios prontos
  sem fonte ativa correspondente (IDs 229096-229110). Nao alterar o banco pelo
  cliente; o pipeline do servidor deve reconciliar esses 15 registros.
- Limitacao honesta: compilacao e contrato nao executam o NRO ARM64. A confirmacao
  final continua sendo no Switch real: filme R2, episodio R2 e anime MP4 por pelo
  menos 30 s, depois seek e retorno. Se falhar, a nova `Ultima etapa` e obrigatoria
  para a proxima rodada; nao reintroduzir recuperacao interna por `goto`.

## Versao diagnostica persistente em 01/09/2026 (0.9.8)

- Novo relato real da 0.9.7: filmes e series ainda fecham o processo no Switch e
  a abertura das respectivas telas continua lenta. Como o processo morre sem
  devolver um erro C, a 0.9.8 prioriza evidencia persistente antes de outra troca
  especulativa do transporte/decoder.
- `source/diag.c` grava a tentativa atual em
  `sdmc:/switch/.nplay-player-trace.log` e preserva a anterior em
  `.nplay-player-trace.prev.log`. Cada linha inclui sequencia, ticks, memoria
  usada/total e a fronteira concluida: format/probe, recursos HLS, streams,
  NVTEGRA/CPU, audio, primeiro pacote/frame, textura, primeiro Present e limpeza.
- O trace de rede em `.nplay-network-trace.log` registra endpoint sem query,
  codigo HTTP, duracao e tamanho da resposta. URL assinada, token, senha e headers
  nao sao persistidos. O arquivo gira automaticamente ao atingir 64 KB.
- Configuracoes > X carrega uma copia dos ultimos seis eventos do player e das
  duas ultimas requisicoes. A leitura ocorre somente ao abrir o modal, nao a cada
  frame. Depois de um crash, reabrir o app nao apaga o trace; ele so gira quando
  uma nova reproducao e iniciada.
- Validacao limpa passou sem warnings: contrato site/Switch, simbolos HLS/TLS/
  NVTEGRA e build ARM64. `Nplay.nro` possui 23.581.232 bytes e SHA-256
  `07dc1be72f0bccb9f9d20dea86d8ef0f34a462033c708f34b3e715d1a085702d`.
  O contrato integrado do backend `scripts/player-flow-contract-test.mjs` tambem
  passou.
- Teste obrigatorio no hardware: instalar 0.9.8, tentar um filme R2 uma unica vez;
  se fechar, reabrir e fotografar Configuracoes > X sem iniciar outro titulo.
  Repetir com um episodio de serie. O ultimo evento separara crash em abertura,
  probe, decoder, audio, primeiro frame ou renderer. Registrar tambem as duas
  linhas de rede para atacar a latencia de detalhe/catalogo na rodada seguinte.

## Correcao orientada pelo trace real em 01/09/2026 (0.9.9)

- A foto da 0.9.8 localizou o fechamento entre `avio http` do primeiro recurso e
  o retorno de `avformat_open_input`: nenhum evento de probe, decoder, audio,
  primeiro frame ou renderer ocorreu. O unico AVIO ativo era metadata HLS com
  352 KB. Portanto a falha nao estava na decodificacao nem na GPU.
- A causa atacada e o ciclo de vida do primeiro manifesto: `io_open` devolvia um
  AVIO ao FFmpeg antes de a thread produtora terminar a primeira resposta. O
  demuxer e a thread podiam observar/alterar `size`, EOF e buffer simultaneamente
  justamente durante `avformat_open_input`.
- Playlists, WebVTT/SRT e chaves HLS agora sao baixadas, limitadas, validadas e
  congeladas antes de `io_open` retornar. O AVIO pequeno suporta leitura e seek
  diretamente no buffer imutavel, sem thread. Segmentos de midia continuam no
  prefetch assincrono de 1 MB para preservar throughput e fluidez.
- O parser de `Content-Range`/`Content-Length` nao chama mais `atoll` em memoria
  de header do curl que nao tem garantia de terminador NUL; copia para um buffer
  local terminado e valida multiplicacao de tamanhos antes dos callbacks.
- O valor `mem=3185/3189MB` da 0.9.8 era a reserva do processo Horizon, nao 4 MB
  fisicamente livres. O novo trace mostra `heap=usado/livreKB` via `mallinfo` e
  mantem `proc=...MB` apenas como contexto, evitando diagnostico enganoso.
- Build limpo 0.9.9 e contrato site/Switch passaram. `Nplay.nro` possui
  23.581.232 bytes e SHA-256
  `7b3ec3a4839d6732aa99cc446718dd31f6f236a703f381e372c93894f578216c`.
- Teste de hardware: filme e episodio HLS por 30 s. Se ainda fechar, a ultima
  linha deve agora ser anterior a `metadata-ready`, entre `metadata-ready` e
  `hls-io open-ok`, ou ja em outro recurso/etapa; isso define a proxima correcao
  sem reabrir a hipotese de decoder antes da evidencia.

## Causa raiz da abertura confirmada em 01/09/2026 (0.10.0)

- A foto real da 0.9.9 avancou ate `metadata-ready` e `hls-io open-ok`, com
  aproximadamente 62 MB livres no heap, mas nao chegou a `format open-ok`.
  Isso exclui download incompleto, falta de memoria, decoder, audio e renderer.
- A revisao do FFmpeg 7.1 usado no NRO confirmou a fronteira: ao receber o
  manifesto raiz por `io_open`, `avformat_open_input` ainda executava o probe
  generico sobre esse AVIO antes de entrar em `hls_read_header`. Era exatamente
  a etapa entre os dois breadcrumbs das capturas e o processo Horizon morria
  dentro dela sem devolver codigo C.
- O root HLS agora e aberto explicitamente por libcurl antes do FFmpeg, ligado a
  `fmt->pb` como `AVFMT_FLAG_CUSTOM_IO`, e `av_find_input_format("hls")` e passado
  a `avformat_open_input`. A URL original continua no contexto para resolver
  filhos relativos; `io_open/io_close2` atendem apenas playlists, init e segmentos
  internos. Assim o probe instavel do manifesto raiz nao e mais executado.
- Um manifesto R2 real foi consultado de forma somente leitura e com token
  ocultado: master valido, H.264 1080p, dois audios AAC e duas legendas. A analise
  tambem confirmou que URLs assinadas possuem query longa; o cliente mantem
  `allowed_extensions=ALL`, necessario no FFmpeg 7.1.
- Para a lentidao inicial, apos carregar Home o cliente aquece Filmes, Series,
  Animes e Doramas em uma unica thread e nessa ordem. Nunca existem dois fetches
  de catalogo simultaneos; uma falha automatica nao entra em loop e abrir a aba
  permite tentar novamente.
- Build limpo 0.10.0 passou junto com contrato site/Switch e contrato integrado
  do backend. `Nplay.nro` possui 23.581.232 bytes e SHA-256
  `08addadd0ed81135b4afd07285d321ba5bbb6fa24dbaa0c45b3a3bfed170b348`.
- Teste obrigatorio: apos abrir o app, aguardar Home estabilizar e alternar para
  Filmes/Series (cache aquecido); reproduzir filme e episodio HLS por 30 s. Se
  houver nova falha, o trace deve obrigatoriamente passar por `root-ready` e
  localizar uma etapa interna diferente; nao voltar ao probe generico do root.

## Correcao de pressao de memoria em 01/09/2026 (0.10.1)

- A captura real da 0.10.0 confirmou que o root HLS e o demuxer ja estavam
  funcionando: o trace avancou por centenas de aberturas internas. O encerramento
  aconteceu com `heap=192087/14136KB`, isto e, somente cerca de 14 MB livres.
  Na captura 0.9.9 a mesma fase com menos catalogos retidos tinha cerca de 62 MB.
- Foi removido o aquecimento automatico de Filmes, Series, Animes e Doramas. Ele
  reduzia o tempo da primeira troca de aba, mas mantinha cinco payloads grandes e
  suas capas vivos justamente durante o maior pico de memoria do FFmpeg.
- Antes de cada `player_run`, `playback_memory_enter` libera as cinco landings,
  todas as texturas e surfaces de capa e esvazia downloads de capas ainda na fila.
  Os tres workers ficam suspensos; uma imagem que termine durante o player e
  descartada em vez de concorrer com playlists, demuxers e decoders.
- Busca, detalhe da obra, serie, Historico e posicao permanecem em memoria para
  preservar o retorno contextual. Ao voltar para uma landing, somente a aba
  visivel e recarregada de forma assincrona.
- O titulo do player e copiado antes da liberacao dos catalogos, evitando ponteiro
  pendente ao tocar episodios/canais diretamente a partir de uma rail.
- Build 0.10.1 concluido sem erros nem avisos. Pendente no Switch: confirmar que o
  primeiro evento do novo trace mostra margem de heap substancialmente maior e
  reproduzir um filme e um episodio por 30 s. Se ainda houver falha, usar a foto
  do trace 0.10.1; ela mostrara se o consumo restante pertence ao demuxer HLS.

## Inicio da reconstrucao 0.11.0 em 23/09/2026

- Plano e criterios de aceite em `docs/SWITCH_REBUILD_PLAN.md`.
- `catalog_fetch` tira busca e detalhes de filme/serie (inclusive relacionados)
  da thread SDL. Uma requisicao ativa e no maximo uma intencao pendente evitam
  tempestade de chamadas ao trocar de tela; B volta sem esperar a rede.
- O atualizador agora exige tamanho, SHA-256 publicado no GitHub Release e
  cabecalho NRO valido antes de substituir o arquivo instalado.
- `.gitattributes` preserva os bytes de `data/cacert.bin` no checkout Windows;
  converter suas quebras de linha impedia a verificacao da release.
- A 0.11.0 ainda NAO foi publicada. Proximo passo: validar no hardware os estados
  de rede lenta/cancelamento, memoria e player, depois seguir com o shell nativo.

## Perfis nativos na branch 0.11.0 em 23/09/2026

- `Trocar perfil` consulta `/api/account/profiles` fora da thread SDL. A escolha
  persiste na microSD e `net.c` envia `X-Profile-Id` apenas para chamadas
  autenticadas `/api/` do Nplay.
- Troca durante uma sessao reinicia o NRO para descartar respostas, cache e
  threads do perfil anterior. O primeiro login seleciona o perfil sem reinicio.
- Listas locais ficam em arquivos por perfil; o arquivo legado e associado ao
  primeiro perfil escolhido pela conta anterior, identificada por `user.txt`.
  O perfil salvo e validado contra a conta em cada boot. Nao apagar
  `media_lists.json` na migracao.
- Build ARM64 e contratos estaticos passaram; teste de isolamento e reinicio no
  Switch real ainda e obrigatorio antes de uma GitHub Release `latest`.

## Fileiras e medicao de quadros na branch 0.11.0 em 23/09/2026

- A Home agora calcula o tamanho das fileiras quando aplica o catalogo e so
  desenha linhas/cards visiveis. Isso reduz varreduras de cJSON por quadro sem
  ampliar o cache de catalogo ou mudar a navegacao.
- Configuracoes > X mostra quadros da UI acima de 20 e 33 ms e pior tempo desta
  sessao. Medir no Switch real com navegacao longa; o contador nao inclui o
  tempo dentro do player e nao persiste apos fechar o app.
- A busca reutiliza contagens por filtro do resultado atual e desenha os
  resultados em uma passagem. Evitar indices duplicados de cJSON em memoria:
  o player ja teve fechamento real por pressao de heap.
- Home e busca usam clip de conteudo entre topbar e rodape. `text_clip` e
  `text_center_at` preservam o clip anterior; manter isso se refatorar UI,
  senao cards voltam a cobrir a navegacao durante a rolagem.
- O limite vertical das fileiras inclui os 40 px de titulo/foco abaixo da capa.
  Favoritos sao carregados por `CatalogFetch` apos selecionar perfil, com
  cancelamento no fechamento; nao voltar a chamar `api_get` na thread SDL.
- A 0.11.0 foi preparada para atualizar a partir do aplicativo antigo a pedido
  do usuario; executar no console o roteiro em
  `docs/SWITCH_0_11_HARDWARE_CHECK.md` e corrigir falhas observadas.

## Identidade e atualizacao 0.11.0 em 23/09/2026

- `icon.jpg` foi derivado do icone 512x512 vigente no site Nplay, convertido
  para JPEG 256x256 exigido pelo hbmenu. O JPEG foi encontrado byte a byte no
  NRO gerado.
- O asset publico v0.10.1 foi baixado e seu SHA-256 conferido com o digest da
  release. Ele contem `/releases/latest`, download `.nro` e busca dos caminhos
  Nplay/Meruem. A versao instalada pelo usuario ainda precisa ser identificada
  e a atualizacao confirmada no console; o build local nao prova essa etapa.

## Consolidacao do player em 29/09/2026 (0.12.26)

- Worktree usada: `C:/NplaySwitch/.codex-tmp/switch-0.12.18`, branch
  `codex/switch-0.12.18`, baseada em `origin/codex/switch-rebuild` 0.12.25.
- `source/player.c`: o worker de demux ganhou barreira cooperativa. Seek e troca
  de faixa so alteram `AVFormatContext` depois que `av_read_frame` confirmou
  pausa; pacotes antigos sao limpos antes da nova geracao. Operacoes normais nao
  reabrem mais manifesto, probe e decoders completos.
- Falha/cancelamento depois de tocar o demuxer nunca retoma estado parcial:
  aciona a reabertura controlada na posicao segura. Se nenhum quadro reaparecer
  em 20 s, ha fallback automatico. `SDL_QUIT` nao e mais consumido como B.
- O callback de interrupcao so le estado nao atomico na thread de render; a
  thread de demux observa exclusivamente `demux_abort` atomico.
- A fila SDL fica pausada durante preroll e so volta no primeiro quadro novo.
  `SDL_QueueAudio` e verificado. Pausa longa e reancoragem do relogio possuem
  simulacao de regressao.
- Renditions HLS usam metadata `title`, `comment` e `name`; o fixture confirmou
  que FFmpeg publica `NAME` como `comment`. A politica seleciona PT-BR mesmo com
  ingles DEFAULT quando a conta pede Dublado.
- Legendas recebem `pkt_timebase`, calculam WebVTT por `AVSubtitle.pts`/PTS e
  duracao do pacote, registram pacotes/cues e mantem 32 cues (aprox. 17 KB).
- `source/main.c`: Historico/Biblioteca propagam `m3u8` e `DELIVERY_R2` ao abrir
  um item preparado, evitando o caminho de arquivo simples.
- `tools/test_hls_player_fixture.mjs` gera localmente HLS fMP4 com dois audios e
  duas legendas, testa metadata, abertura, seeks e decodificacao. Foi integrado
  a `tools/validate_release.ps1`.
- Validacao limpa final: build ARM64 `-Werror` e suite completa passaram;
  fixture: 5 faixas/2 cues; remux: 203 quadros. `Nplay.nro` tem 24.155.983 bytes,
  SHA-256 `42601cc290c2f145a555d24d169f5fbf395ffce216fed61b0d5dfd48f8f844a7`.
- Pendente obrigatorio: teste no Switch real de filme, serie, anime e dorama,
  pausa/retomada, seeks, audio PT-BR, legendas e perda curta de Wi-Fi. Nao afirmar
  comprovacao em hardware com base apenas no build local.

## Proximo episodio integrado em 29/09/2026 (0.12.27)

- Worktree de continuidade: `C:/NplaySwitch/.codex-tmp/switch-0.12.18`, branch
  `codex/switch-0.12.18`, sobre a 0.12.26 publicada.
- O HUD possuia botao/cartao de proximo episodio, mas `source/player.c` forcava
  `has_next=0`. Agora o player recebe o contexto real da serie, mostra o botao e
  abre o cartao com direcional direito. `A` confirma; `B`, `-` ou esquerda
  cancelam sem sair do video. Toque no botao/cartao tambem funciona.
- O cartao aparece automaticamente nos 45 segundos finais mesmo com o HUD oculto.
  A escolha retorna `EXIT_REASON_NEXT_EPISODE`, limpa a pipeline normalmente e
  so depois entra no episodio seguinte. Autoplay desligado nao bloqueia uma
  escolha manual e a segunda confirmacao fora do player e ignorada nesse caso.
- Pular cedo apenas salva progresso; nao marca conclusao artificialmente. Audio
  e idioma continuam pela politica de continuidade ja validada na 0.12.26.
- `player_next.c` e `test_player_next.c` tornam a janela visual reproduzivel no
  host. A suite completa e o build ARM64 com `-Werror` passaram depois do bump:
  `Nplay.nro` tem 24.160.079 bytes e SHA-256
  `7532c5fe37c2d3e48037ea55ea7a7b1be831bfaa50aaf1208e5534f156aa2f25`.
- Nao foram adicionados seletor de qualidade, velocidade ou pular abertura nesta
  rodada. Qualidade atualmente so possui contrato multiplo consistente no fluxo
  de anime; velocidade exige tratamento de audio e pular abertura depende do
  AniSkip externo no site. Implementar isso sem contrato nativo seria regressao.
- Pendente obrigatorio no Switch real: serie com 2+ episodios, abrir/cancelar/
  confirmar o cartao, autoexibicao nos 45 s finais e continuidade PT-BR/legenda.

## Correcao de piscar e audio inicial em 29/09/2026 (0.12.28)

- Teste real da 0.12.27 em X-Men mostrou `Preparando video` e `Aguardando dados`
  alternando na mesma tela antes do primeiro quadro. A causa era dupla
  apresentacao: o loop principal desenhava buffering enquanto o callback de
  interrupcao ainda apresentava o loader de abertura.
- `player_loading.c/.h` define o dono da tela. O callback desenha somente em
  `OPENING` ou em uma `OPERATION` sincrona; ao iniciar o worker de demux, muda
  para `PLAYBACK` e somente o loop principal pode chamar `SDL_RenderPresent`.
  O callback continua ativo para cancelamento e timeout, sem tocar no renderer.
- A serie podia abrir em ingles porque a variante-base `Legendado` sobrescrevia
  `audioPref=dub`. `audio_effective_preference` estabelece: versao escolhida
  explicitamente por ZL/ZR > conta > variante automatica. `Tanto faz` continua
  usando a versao atual; escolha manual no player continua acima dessas regras
  durante a mesma reproducao.
- `FetchIntent.series_audio_explicit` acompanha a intencao pela consulta
  assincrona, inclusive quando ha uma requisicao enfileirada. Troca de temporada
  conserva essa intencao sem fazer uma entrada normal parecer escolha manual.
- Testes nativos `test_player_loading` e `test_audio_policy` reproduzem os dois
  defeitos. `tools/validate_release.ps1` passou apos rebuild limpo: contrato do
  site, relogio, proximo episodio, HLS, WebVTT, TorBox/R2, ordem de episodios,
  remux (203 quadros) e fixture HLS (5 faixas/2 cues/inicio+seek).
- Artefato 0.12.28: `Nplay.nro`, 24.160.079 bytes, SHA-256
  `1b58ad6d8d51fcee844f36d2f0c2796767aed2c0d22080818ed3a98a2ef05040`.
- Pendente obrigatorio no Switch: Series > X-Men > episodio, confirmar uma unica
  tela de espera e PT-BR inicial; depois ZL/ZR para Legendado deve abrir original.

## Auditoria ponta a ponta do player em 29/09/2026 (0.12.29)

- A auditoria completa esta em `docs/PLAYER_END_TO_END_AUDIT_0_12_29.md`. Ela
  acompanha resolucao, HLS/arquivo, demux, decode, clocks, pausa, seek, faixas,
  recuperacao, proximo episodio, sincronizacao e liberacao de recursos.
- Falha concreta corrigida: sair podia esperar heartbeat (ate 6 s), fazer outro
  POST de progresso (ate 6 s) e depois `/stop` sincrono. Progresso final e stop
  agora rodam no worker; a UI concede 1.200 ms e cancela libcurl depois disso.
  Posicao ja salva nao e enviada novamente.
- Falha concreta corrigida: refresh/fail/fallback eram sincronos na thread SDL.
  `player-recovery` executa rede; a UI anima a cada 16 ms e `B`/`-` cancela DNS,
  TLS ou transferencia usando `net_request_timeout_cancel` em todas as etapas.
- `PlayerProgressCallback`, `PlayerHeartbeatCallback`, `PlayerRenewCallback` e o
  novo `PlayerStopCallback` recebem cancelamento atomico. Ao alterar esses
  contratos, manter o userdata do chamador e reunir a thread antes de liberar a
  struct local.
- `player_sync.c` isola a politica testavel: progresso final somente com quadro e
  avancos de 2 s; tolerancia maxima de saida de 1.200 ms. O teste host e
  `tools/test_player_sync.c` e faz parte de `validate_release.ps1`.
- Validacao limpa completa passou: build ARM64 `-Werror`, contrato do site,
  relogio, sync, proximo episodio, loader, PT-BR, HLS, WebVTT, hot stream,
  episodios, remux (203 quadros) e fixture (5 faixas/2 cues/inicio+seek).
  `Nplay.nro`: 24.160.079 bytes; SHA-256
  `16e67bfaa630183d6be8a929229011dc95d854b8deeda35ca389bb8fb074fffc`.
- A fila SDL de audio continua apenas instrumentada. Nao impor descarte/teto sem
  medir `max_audio_queue`, underruns e timestamp no hardware: descartar amostras
  adiantadas cria silencio e dessincronizacao mais tarde.
- Pendente obrigatorio: executar o roteiro de hardware da auditoria, sobretudo
  cancelamento durante Wi-Fi desligado, retorno depois de pausa/seek, PT-BR,
  legendas e tempo de saida. Build/simulacao local nao valida NVDEC, driver SDL
  de audio nem a pilha Wi-Fi do console.

## Prioridade manual de audio e origem das legendas em 29/09/2026 (0.12.30)

- Captura no Switch confirmou duas faixas de audio corretamente identificadas
  (`Portugues/pt` e `Japones/ja`), mas o check inicial voltava ao japones. A
  normalizacao estava correta; a politica ignorava `pref_audio_<perfil>.txt`
  quando `audioPref` da conta era Dublado ou Legendado.
- A ultima escolha manual salva agora vence a preferencia geral da conta entre
  obras e episodios. Uma versao Dublado/Legendado escolhida explicitamente no
  detalhe da serie continua vencendo a escolha salva. Alterar a preferencia em
  Configuracoes limpa a escolha manual anterior.
- `PlayerRequest.audio_pref_explicit` transporta essa intencao ate o player. O
  trace `streams/selected` registra preferencia, flag explicita e idioma salvo,
  sem URL/token. `test_audio_policy.c` cobre conta Legendado + manual PT e conta
  Dublado + manual estrangeiro.
- A mesma captura mostrou somente `Desligadas`: o demux recebeu zero streams de
  legenda. O fixture local do NRO continua abrindo duas rendicoes WebVTT e cues
  antes/depois de seek; portanto a falta nao nasce no modal nem no decoder.
- Causa no backend: `hls-preparer.js` ainda gerava/reutilizava o hash
  `multitrack-v3`. Pacotes antigos sem legenda eram aceitos mesmo quando o probe
  atual encontrava legendas de texto. A correcao esta isolada em
  `C:/NplaySwitch/.codex-tmp/backend-subtitles`, branch
  `codex/switch-subtitles`: schema `multitrack-v4-subtitles`, validacao do master
  e falha se nenhuma faixa de texto puder ser convertida.
- O contrato HLS, `npm run check` e `npm run test:media-worker` passaram no
  backend com FFmpeg. Ainda nao houve deploy nem reprocessamento de producao.
  Pacotes ja publicados precisam de reparo controlado; nao disparar lote em
  massa sem aprovacao, capacidade e observacao do worker/R2.
- Legenda bitmap PGS continua fora: FFmpeg nao a transforma em WebVTT sem OCR.
  Nao anunciar disponibilidade quando a fonte possui somente bitmap.
- Documento de release e continuidade: `docs/RELEASE_0_12_30.md`.
- Validacao final limpa passou na 0.12.30. `Nplay.nro` tem 24.160.079 bytes e
  SHA-256 `4eb1ad838a5c1d9cc49205b316d3afc788f4a08f8dbe945a5197b984ea08dfea`.

## Fallback de legendas HLS e seek confirmado em 29/09/2026 (0.12.31)

- Um caso real mostrou tres legendas portuguesas no player web e nenhuma no NRO.
  O site ja possuia um fallback que le `EXT-X-MEDIA` diretamente do master quando
  o hls.js nao publica as tracks; o cliente Switch dependia apenas dos AVStreams
  criados pelo demuxer FFmpeg.
- O NRO agora extrai nome, idioma e URI das renditions de legenda diretamente do
  manifesto. Se o FFmpeg nao criar nenhum AVStream, o menu ainda lista as tracks e
  abre somente a playlist WebVTT escolhida. Seus cues ficam em uma colecao propria,
  sem reabrir nem alterar a pipeline principal de video/audio.
- URLs relativas de legenda herdam o token assinado do master. A URL efetiva apos
  redirect e preservada pelo AVIO para resolver corretamente playlists do R2.
- L/R e ZL/ZR nao fazem mais um seek/reconnect por toque. O primeiro toque abre a
  pre-visualizacao, novos toques acumulam 10/60 s, `A` executa uma unica busca e
  `B` cancela. O analogico continua usando o mesmo fluxo confirmado.
- Validacao local: build ARM64 com `-Werror`, suite `validate_release.ps1`, parser
  de master, simulacoes de clock/sync, remux e fixture HLS/WebVTT passaram. Pendente
  obrigatorio no hardware: abrir a mesma obra das capturas, conferir tres legendas,
  alternar audio/legenda repetidamente e buscar inicio/meio/fim sem crash.

## Avatares e seletor de perfis em 29/09/2026 (0.12.32)

- Causa da foto ausente: chaves `char:` dependiam de `/api/account/avatars`, mas
  essa consulta so iniciava tres segundos depois da Home. No seletor inicial o
  mapa ainda nao existia. Agora a consulta comeca assim que os perfis chegam,
  sem bloquear a UI, e tem ate duas retentativas espaçadas se a rede falhar.
- Avatares `dice:` do site apontam para SVG, formato nao decodificado pelo
  SDL_image do NRO. O Switch pede a variante PNG 256 px pelo espelho `/api/img`.
  Imagens locais e personagens continuam usando as URLs do catalogo.
- `profile_avatar_url` deixou de varrer ate 256 itens a cada avatar/quadro. Um
  hash fixo de 512 entradas e montado uma vez ao instalar o catalogo; nao aloca
  memoria por frame e continua integrado ao cache/LRU de capas.
- `ui_avatar` usa `SDL_RenderGeometry` para crop cover circular com aro, sem criar
  textura-alvo ou mascara por quadro. Seno/cosseno dos 48 segmentos sao calculados
  uma unica vez. A inicial de fallback escolhe cor clara/escura por luminancia.
- O seletor mostra quatro perfis com retratos de 164 px; o selecionado cresce para
  184 px e recebe aro/foco. O picker passou de 18 miniaturas de 84 px para dez
  retratos circulares de 130 px por pagina. Topbar, menu e editor tambem ficaram
  circulares. Coordenadas de toque e D-pad foram atualizadas junto.
- Pendente obrigatorio no hardware: capturar seletor com 1/3/4 perfis, avatar
  `char:`, `img:` e `dice:`, aguardar retentativa sem Wi-Fi, trocar avatar e
  conferir topbar/menu/editor em 1280x720 sem overscan.

## Destaque do perfil no cabecalho em 29/09/2026 (0.12.33)

- Relato no hardware confirmou que a foto voltou, mas os 46 px anteriores ainda
  pareciam um icone escondido ao lado da busca. O retrato ativo agora usa 70 px,
  quase toda a altura util do cabecalho de 95 px, sem deslocar as prateleiras.
- O aro passou a 5 px na cor de destaque e ganhou um indicador circular de sessao
  no canto inferior. A busca foi deslocada apenas 14 px para manter respiro; a
  ordem e as dimensoes do restante da navegacao nao mudaram.
- O menu rapido ampliou a foto de 72 para 86 px e reposicionou nome/subtitulo,
  mantendo as tres acoes e os hit-tests anteriores.
- A validacao de release agora impede regressao silenciosa para o avatar pequeno.
  Pendente no hardware: conferir a topbar em todas as cinco abas, nome longo no
  menu rapido e overscan nos modos portatil e dock.

## Touch direto e gestos continuos em 29/09/2026 (0.12.34)

- Causa do comportamento de touchpad: o loop guardava apenas o inicio/fim do
  dedo e, ao soltar, convertia o deslocamento em um unico `JOY_UP/DOWN/LEFT/RIGHT`.
  `SDL_FINGERMOTION` nao movia a interface; o usuario arrastava sem feedback e a
  tela saltava como se um Joy-Con tivesse sido pressionado.
- `touch_input.c` agora reconhece um dedo por vez, mantem jitter abaixo de 12 px
  como tap, trava horizontal/vertical no primeiro arraste real e calcula velocidade
  suavizada. O modulo nao depende de SDL e tem teste host dedicado.
- Home, busca, sagas, Historico, Biblioteca, listas, temporadas, episodios e
  relacionados movem o conteudo pixel a pixel enquanto o dedo acompanha a tela.
  Uma inercia curta e limitada continua ao soltar; qualquer comando do Joy-Con a
  interrompe. Soltar depois de arrastar nunca abre um card.
- Scroll horizontal passou a ser persistente por prateleira. O foco e atualizado
  para o item proximo ao dedo somente ao fim do gesto, preservando uma transicao
  previsivel para D-pad/analogico sem sacrificar a manipulacao direta.
- Swipes na barra de abas, destaque e paginas de avatar exigem distancia explicita
  e ficam restritos a essas superficies. Taps continuam usando hit-test absoluto.
- `SDL_HINT_TOUCH_MOUSE_EVENTS=0` e eventos SDL de mouse ignorados evitam clique
  duplicado sintetizado. A implementacao segue coordenadas normalizadas e eventos
  `FINGERDOWN/MOTION/UP` do SDL2 e o touchscreen absoluto exposto pelo libnx.
- O painel de audio/legendas do player deixou de ignorar toque: uma faixa tocada
  usa a mesma troca transacional do botao A; tocar fora fecha. Timeline ja usava
  posicao absoluta e foi preservada.
- Documento de release e continuidade: `docs/RELEASE_0_12_34.md`.
- Validacao limpa completa passou: build ARM64 com `-Werror`, contrato do site,
  clock/sync/loading/proximo episodio, touch, politica de audio, manifesto HLS,
  legendas, API, episodios, remux e fixture multifaixa. `Nplay.nro` tem
  24.176.463 bytes e SHA-256
  `1faf37b4fd7d9b1a4a1731ad100c55a8a62c125e1eb80650af5525c44b6cc16b`.
- Pendente obrigatorio no hardware: validar tap com tremor, arraste lento, diagonal,
  fling, bordas, segundo dedo, troca touch/controle, todas as superficies listadas,
  timeline e painel de audio/legendas. Build/simulacao nao valida o driver touch do
  Switch nem sensacao de inercia no painel fisico.

## Onboarding e pareamento por celular em 30/09/2026 (0.12.35)

- A entrada principal agora e um fluxo de dispositivo inspirado no OAuth Device
  Authorization Grant: `A` gera QR/codigo, o celular faz login, cadastro ou entrada
  como visitante e confirma o Switch. Usuario/senha continua disponivel por `Y`.
- `device_pairing.c/.h` valida codigo publico, segredo, URL HTTPS, matriz QR com
  teto de 69 modulos, token e respostas do polling. QR invalido cai para o codigo
  digitavel; uma resposta estruturalmente invalida nunca autentica o console.
- Rede e espera executam em `login_pairing_thread`. A UI continua responsiva; `B`
  cancela inclusive libcurl, `X` renova o codigo, `slow_down` soma 5 s e falha de
  rede usa backoff ate 30 s sem abandonar um codigo ainda valido.
- O backend complementar esta em `C:/NplaySwitch/.codex-tmp/backend-device-onboarding`,
  branch `codex/switch-device-onboarding`: `/api/device/code` devolve a matriz,
  usa `no-store` e rate limit; o site preserva `#/pair` entre login, cadastro e
  visitante. O commit `c650809` foi implantado pela execucao `36797192004`.
- A cota do visitante permaneceu inalterada de proposito. Antes de ampliar para
  “3 conteudos”, definir se sao titulos completos ou previas e qual janela/escopo;
  o comportamento atual e uma previa unica protegida por tempo, dispositivo e IP.
- Testes adicionados: `tools/test_device_pairing.c`, dois contratos web/API em
  `npm run test:tv-pairing` e rotas de dispositivo em `audit_switch_routes.mjs`.
- Validacao limpa passou na 0.12.35: 24.184.655 bytes, SHA-256
  `4bf8a54555c38189898dc5b0297d6a3541ce25aee48e9c9db97db0f2a342eebc`.
  A API publica foi validada depois do deploy: HTTP 200, `Cache-Control: no-store`,
  codigos presentes e QR `bit-rows-v1` de 29 x 29 modulos. O GitHub Actions foi
  habilitado temporariamente e restrito a `appleboy/ssh-action@*`; desabilitar de
  novo depois de publicar e verificar a release 0.12.35.
- Pendente obrigatorio no hardware: QR em cameras Android/iPhone, login/cadastro/
  visitante, escolha de perfil, cancelar/renovar/expirar, Wi-Fi oscilando, toque e
  legibilidade portatil/dock. Simulacao local nao testa camera nem rede do Switch.

## Preferencia da conta e retorno por touch em 01/10/2026 (0.12.36)

- A regra de 0.12.30 que deixava idioma manual antigo vencer a conta em outras
  obras foi revista apos relato real. Dublado/Legendado volta a controlar entrada;
  idioma persistido so vale em Tanto faz. Prioridade manual 1/2 da MESMA
  reproducao permanece intacta. O teste novo falhou antes da correcao e passou depois.
- ui_header mede o texto da acao a direita e ui_header_action_hit compartilha essa
  geometria com o tap. O hit-test anterior so reconhecia o lado esquerdo e ignorava
  o Voltar visivel. Serie/Anime/Dorama, Filme, Saga, Perfis, Config e Loading usam
  o helper; o modal de retomada tambem reconhece Cancelar pelo cabecalho.
- Auditoria e continuidade: docs/AUDIT_AUDIO_SUBTITLES_TOUCH_2026_10_01.md.
  601785c do backend permanece fora do main; v3 nao significa que todos os pacotes
  carecem de legendas. Nao declarar legendas de producao corrigidas. Portar/testar
  o empacotador e fazer reparo controlado da obra afetada, sem lote em massa.
- Fila demux 32 pacotes/4 MiB, 15 mutacoes sincronas e SDL sem teto sao pendencias
  confirmadas; causa de engasgo exige correlacao com trace/PTS/bytes no console.
- Build ARM64 -Werror e suite completa passaram: 24.184.655 bytes, SHA-256
  8f78205f56a2380644b0876a7bab92363af0037c222ca1f10b8f9ed445361b73.
- Publicar binario apenas como asset de Release nesta rodada; nao inclui-lo no
  commit. Pendente fisico: Voltar vindo de busca/Home, tap/arraste, cancelar
  retomada, Dublado com ingles/japones salvo e escolha manual/reconnect.
- Publicacao concluida: commit de codigo f73ee28 em codex/switch-rebuild, GitHub
  Release v0.12.36 publica/latest. O endpoint releases/latest confirmou tamanho
  24184655 e digest exato. Nplay.nro permanece modificado localmente por ser
  artefato regenerado; nao incluir esse binario em commits posteriores.

## Estabilidade e auditoria 0.12.37 em 01/10/2026

- Continuidade completa e evidencias em docs/RELEASE_0_12_37.md. Fila 128 slots /
  4 MiB, pending packet limitado separado, geracao para invalidar leitura anterior
  ao seek, telemetria de reserva de video. Testes usam worker C real com pthreads.
- Mutacoes da UI e progress GET sairam da thread de rede/desenho compartilhada:
  ui_request mantem tela responsiva e aguarda join antes de liberar estado.
- CURLSH nao compartilha mais conexoes concorrentes; DNS/TLS protegidos mantidos.
- Backend v4 preparado em backend-subtitles-v4. Nao dizer que todas as legendas
  estao resolvidas: fontes reais divergem (0 e 8 renditions), uma respondeu 404,
  hot probe/VTT do PC ainda nao e implementado no NRO. Nenhum reprocessamento em lote.
- Build ARM64 limpo e suite completa passaram. Hardware e Linux nao testados.
- Publicado 91da091 em main e codex/switch-rebuild por fast-forward atomico.
  Release latest v0.12.37 confirmada com digest e tamanho exatos pelo GitHub.
  Nplay.nro deixou de ser rastreado, preservado no disco e como asset da Release.

## Expiração de acesso e recuperação 0.12.40 em 03/10/2026

- O NRO reconhece `401` com `reason=expired` no catálogo, na abertura e na
  recuperação do player. A sessão local é removida e a tela retorna ao login,
  sem deixar o usuário em reconexão infinita. Toque no botão central de cancelar
  também interrompe a tela de recuperação.
- Após 12 s sem quadro nem áudio em HLS já iniciado, o player escala a falha ao
  supervisor para renovar a sessão; troca de faixa e áudio ainda em fila não
  acionam essa escalada. A política tem teste de host próprio.
- O backend correspondente deve limitar tokens R2 ao `access_expires_at` e
  validar grant+conta em `/api/media/hot/*/video` e `/api/media/debrid/*/video`.
  A documentação completa está em `docs/SWITCH_ACCESS_EXPIRY_2026_10_03.md`.
- Validação local: compilação ARM64 sem avisos, testes de áudio/touch/recuperação
  e a maior parte de `validate_release.ps1` passaram. A etapa final de símbolos
  depende de `aarch64-none-elf-nm` estar configurado no PowerShell nativo.
  Pendente obrigatório: conta com vencimento real em R2, hot e debrid no Switch.

## Estabilidade de rede 0.12.41 em 03/10/2026

- Trace fornecido da 0.12.38: segmentos R2 com primeiro byte em 21–22 s,
  curl 28 em 30 s e posição presa em 3072 s apesar de downloads posteriores.
  Nenhum 401/403 ou erro de certificado foi observado nesse incidente.
- `cio_read` não retorna mais EAGAIN depois de 300 ms ao parser de segmento
  HLS/fMP4; espera no worker separado, com checagem de cancelamento a cada 100 ms.
  FFmpeg 7.1 registra erro/EOF em `fill_buffer` para retorno negativo.
- HLS sem byte de corpo por 8 s aborta a tentativa e tenta socket novo.
  Corpo parcial/backpressure não dispara esse limite; offsets, TLS e teto
  de memória são preservados. Supervisor de stall 12 s da 0.12.40 permanece.
- Harness extrai/executa callbacks C reais: baseline falhou no gap de 1500 ms;
  corrigido passou, incluindo cancelamento, ring, EOF e prazo sem falso abort.
- Build ARM64 -Werror e `validate_release.ps1 -SkipBuild` completo passaram,
  incluindo fixtures reais e símbolos com TARGET_NM configurado. A versão
  não foi testada no hardware; exigir sessão >60 min e traces se persistir.
- Evidência, comandos e limites: docs/PLAYER_NETWORK_STABILITY_0_12_41.md.
  Publicar NRO só em Release, nunca no commit. Não afirmar ausência de buffering
  com rede ruim, nem que legendas/idioma foram alterados nesta rodada.
- Publicado commit 7c85e75 em codex/switch-rebuild; Release v0.12.41 confirmada
  como latest. Asset Nplay.nro de 24201039 bytes e digest SHA-256 coincidem com
  o build local (afea68abe5dd91d8734eb3a474cff0c812ef05b88e64b57c647fd2cfa0c2fc5b).
- Deploy anterior do backend de expiração: run 37134528626 ainda queued na
  verificação desta rodada. Não declarar implantado. A correção de transporte
  desta release é no cliente e não depende desse deploy. Não contornar política
  de Actions; confirmar conclusão antes de desabilitar o workflow temporário.

## Retomada sem reinício automático — 0.12.42, 03/10/2026

- Usuário confirmou falha na 0.12.41. `resume-from-start` do supervisor era
  causa reproduzível do reinício: removido, renew/fallback conservam checkpoint.
- Seek inicial negativo não prossegue desde zero; pontos <3 s/perto do fim
  não são ignorados. Preroll protege HLS/arquivos; EOF em preroll é erro.
- Remux sequencial sem busca não finge retomada desde início. Pausa/menus/
  timeline não contam na janela de buffering/watchdog de retomada.
- Harness executa `player_run` C real extraído: baseline 7c85e75 falhou no
  seek 3000→3060 após falha de rede; corrigido passou. Pipeline/SDL são stubs,
  não hardware. Build ARM64 -Werror e suíte completa com fixtures passaram.
- Evidência/limites/teste físico pendente: docs/PLAYER_RESUME_0_12_42.md.
  Não afirmar fim de todos os stalls: falta reproduzir no console e obter novos
  traces. O checkpoint é preservado mesmo ao esgotar recuperação/cancelar.
- Publicado d9c2df6 em codex/switch-rebuild; Release v0.12.42 latest verificada
  com asset 24201039 bytes e digest 29cb1dbdff768f1adcd3f27f5ad189ebed0aaa5d49ea8c8a2aec378a2cb218db.

## Resistência a espera local e seek ocupado — 0.12.43, 03/10/2026

- Segmentos HLS não usam mais LOW_SPEED_TIME (contava espera do consumidor).
  xfer_cb detecta 8 s sem corpo; wr_ring rearma idle após conseguir espaço.
  Metadados/arquivos, TLS e memória mantidos. Prova real em libcurl HOST 8.13.0
  mostrou curl 28 com política antiga escalada e conclusão com nova; idle real
  continua abortando. SDK Switch é 7.69.1: efeito físico ainda não comprovado.
- Seek: barreira ocupada sem mutação retorna 5, não reabre. Timeline conserva
  destino com A repetir/B cancelar; touch conserva ponto/reprodução. Falha real
  depois de seek continua reabrindo com checkpoint, sem acesso concorrente a fmt.
- Rebuffer procura 0.75 s de vídeo, somente após 250 ms de starvation; libera
  até 3 s, EOF/erro, slots cheios ou bytes >=75% do teto. Sem aumentar memória.
- Callback C real testou espera local de 60 s; helper de seek antigo falhou e
  atual passou. Worker pthread, snapshot/reserva, supervisor e suíte completa
  com mídia/símbolos passaram, build ARM64 -Werror. Nunca confundir com Switch.
- Evidência e limitações: docs/PLAYER_RESILIENCE_0_12_43.md. Próximo: teste
  físico de pausa longa, menus, busca/touch, perda Wi-Fi e sessão >60 min, com
  traces novos (idle/bytes e reserva). Não declarar solução de todos os stalls.
- Publicado código 66a5ced em codex/switch-rebuild; Release v0.12.43 latest
  confirmada, asset 24201039 bytes e SHA-256 3b3871db55107c100c54d2b66bbedf64518089b869812b26ba251ccb875acfeb.

## Animes, fonte e próximo episódio — 0.12.44, 03/10/2026

- Usuário reportou Super no Ura de Yani Suu Futari com várias legendas no PC
  e nenhuma no NRO. Episódio/trace atual ainda ausentes. Backend origin/main
  a25a576 coloca R2 antes de Animes Drive; teste do resolver C real confirma
  que NRO não substitui delivery=r2. Isso NÃO verifica esse pacote em produção.
  Catálogo público exigiu login (401); não contornar nem usar credenciais alheias.
- Cartão Próximo fica visível em pausa/HUD fixo; Direita seleciona/A confirma,
  B/esquerda cancela. Toque primeiro seleciona, segundo confirma. Não detecta
  créditos: janela final continua 45 s; opção manual não depende dela.
- Contexto direto Home/Histórico usa consulta cancelável única de até 5 s.
  Ordem vem do detalhe inteiro, inclusive troca de temporada; preparo conserva
  apresentação/next e retorno NEXT=2. Consulta agrupada conserva explicit_next,
  não depende de autoplay quando o usuário pediu o próximo manualmente.
- Diagnóstico conserva provedor allowlisted (animesdrive/hinatasoul) sem URL/SID.
  Não mexeu em empacotador, política de áudio ou decoder de legendas. Não há
  migração automática upstream→R2 durante um vídeo, como no site.
- Simulação executa play_episode_sequence real: b283f72 falha em has_next,
  atual passa temporadas fora de ordem, final/grupo, abertura direta/cancelar
  e avanço explícito. API fake valida R2/anime, MP4/provedor e label seguro.
- Build ARM64 completo -Werror e suite completa/fixtures reais passaram.
  Hardware e reprodução específica do anime não confirmados. Continuidade e
  checklist em docs/ANIME_R2_NEXT_0_12_44.md. Publicar NRO só como asset, e
  registrar digest/publicação após build final. Pedir trace novo antes de
  atribuir legenda ausente ao provedor ou anunciar correção de produção.
- Publicação confirmada: be6c68c em codex/switch-rebuild, Release latest
  v0.12.44 não draft/prerelease; Nplay.nro 24205135 bytes, digest SHA-256
  9321a245ef44b7a679781480a08b51c8978c28eb6246e57f201651aef046de55
  idêntico ao build final. NRO não rastreado em Git. Atualizador pode buscá-la.

## Player estavel, seek posicionado e painel Episodios — 0.12.45, 04/10/2026

- Relato: player "emperrado", recarregando a cada avanco/pausa, animes instaveis,
  legendas falhando e sem trocar de episodio no player. Reproduzido com
  `tools/host_player/` (player_run REAL no Linux, FFmpeg 7.1 do wheel PyAV,
  conteudos no formato real do R2/torrent, servidor com latencia). A 0.12.44
  falha 10 das 18 verificacoes de `suite.sh`; a 0.12.45 passa 18/18 em rede boa
  e em Wi-Fi ruim (500 ms, 10 Mbps).
- Causa raiz principal: o seek interno do FFmpeg 7.1 em HLS fMP4 com renditions
  separadas nao reinicia o demuxer mov; le segmentos novos com indice velho,
  descarta o audio ate o fim, NAL invalido e nenhum quadro. Reproduzido tambem
  em PyAV e `ffmpeg -ss`. NAO voltar a chamar avformat_seek_file/av_seek_frame em
  HLS: seek, Continuar e troca de audio usam abertura posicionada
  (`hls_media_playlist_trim` + `nplay_curl_avio_set_hls_start`), com cache curto
  de playlists e `init-*.mp4`. `player_seek_with_barrier` devolve 2 para HLS.
- L/R/ZL/ZR somam saltos e aplicam 700 ms depois do ultimo toque, sem pausar.
  Reabertura mostra o ultimo quadro escurecido (`pui_set_loading_backdrop`).
- Remux sequencial (torrent): duracao vem da sonda (`sequential-duration`); a do
  demuxer (~2 s) fazia o backend marcar o episodio como visto (>=92%). Reserva
  512 pacotes/12 MB e audio adiantado (`demux_worker_take_stream`) eliminam as
  faltas de audio causadas por `frag_keyframe` com GOP longo.
- Legenda ate 4 linhas (dialogo + placa). Faixas `audio_N` exibem o idioma.
- Painel Episodios (esquerda): `PlayerRequest.episodes`, `PlayerResult.chosen_item_id`,
  `play_episodes_build`; escolher o anterior funciona.
- Validacao: ARM64 `-Werror`, 18 testes C, testes Node do validador (atualizados
  para os novos caminhos), 146 assercoes do validate_release (11 novas). Detalhes
  e pendencias de hardware em `docs/PLAYER_STABILITY_0_12_45.md`.
- Pendente no Switch: saltos e Continuar em R2, troca de audio, anime torrent por
  10 min, painel Episodios. Abertura inicial em Wi-Fi ruim ainda ~6 s.

## Abertura paralela, audio unico e legenda assincrona — 0.12.46, 04/10/2026

- Medido com `tools/host_player/` antes de mudar: pausa ja era instantanea; o
  tempo estava na abertura (requisicoes pequenas em fila, todas as faixas de
  audio abertas, legenda baixada antes do 1o quadro). Detalhes, A/B e roteiro
  de hardware em `docs/PLAYER_SPEED_0_12_46.md`.
- `nplay_curl_avio_hls_prefetch`: playlists (variantes primeiro, todos os
  audios) e inits em ate 4 conexoes para o cache curto antes do FFmpeg. Resolver
  como o FFmpeg 7.1: base = URL original (nao a do 302) e SEM herdar query
  (`hls_manifest_resolve_like_ffmpeg`); conferido no host. Ondas limitadas a 5
  para caber nos 12 slots do cache.
- `player_hls_choose_audio` aplica a politica sobre o master e entrega ao
  demuxer um master com uma unica rendition (`hls_manifest_keep_audio`). O
  painel usa `naud_ui`/`acur_ui` (lista do master); `acur`/`aidxs` continuam
  sendo as AVStreams reais. So filtra com 2+ audios com URI no mesmo GROUP-ID.
- Legenda do master: `SubtitleFetch` em segundo plano na abertura e na troca;
  a faixa anterior fica ate a nova chegar. Threads auxiliares usam
  `nplay_curl_avio_set_thread_cancel` (nunca o callback de B da thread de render).
- `g_playback_chain`: Home recarrega uma vez ao fim da sequencia de episodios.
  Progresso salvo e pedido em paralelo com `/stream` (`resolve_progress_thread`).
- A/B (Legendado): 1o quadro rede boa 2,7–3,0 → 1,25–1,5 s; Wi-Fi ruim
  6,3–7,0 → 2,7–3,6 s; troca de legenda deixa de congelar (~1,1 → ~0,1 s).
  Suite 18/18 nas duas redes, testes C/Node e 153 assercoes; ARM64 `-Werror`.
- Atencao: `make clean` apaga `build/` inteiro, inclusive `build/host_player`.
- Pendente no Switch: abertura Dublado/Legendado, trocas Y/X, maratona 3+
  episodios; no trace, `meta-prefetch`, `audio-filter`, `async=1`.

## Legenda de anime que sumia e episodio "sem fonte" — 0.12.47, 04/10/2026

- Relato: anime R2 em japones, a legenda parou de aparecer no meio. Causa
  principal reproduzida na 0.12.44 instalada: teto de 8192 cues (512 B cada).
  Fansub com karaoke vira dezenas de milhares de cues WebVTT; o resto do
  episodio ficava sem nenhuma fala. Detalhes em `docs/SUBTITLES_ANIME_0_12_47.md`.
- `source/subtitle_store.c`: 16 B por cue, area de texto unica, funde repeticoes,
  ordena, busca binaria, ignora desenho ASS; exibe a fala acima de karaoke e
  placas (4 linhas). NAO voltar a `SubtitleCue` fixo de 512 B nem a teto 8192.
- Cues da legenda do master guardados durante a reproducao (`subtitle_session_*`):
  salto/troca de audio/recuperacao nao baixam de novo. Falha ao baixar repete em
  2/5/10/20 s mantendo a faixa escolhida; X na mesma faixa tenta na hora.
  Torrent: legenda progressiva com ate 3 novas tentativas. Teto textual 8 MB.
- Harness: `SUB pos=... [texto]` registra o texto entregue ao HUD; fixture
  `r2sub` (karaoke/placas); `latency_server.py --fail-match`; `suite.sh` com 26
  verificacoes (26/26 em rede boa e Wi-Fi ruim). Binarios antigos para A/B: o
  harness compila contra 0.12.44 com `-DHARNESS_LEGACY_044`.
- Episodio de The Big Bang Theory ja concluido dizendo "sem fonte": o Switch
  envia o item correto; o texto vem do backend (`POST /api/stream/:id` -> 409
  "Nenhuma fonte ativa disponivel para este conteudo" quando `allActiveSources`
  fica vazio, ou 503 em modo `r2_only`). Sem acesso ao banco de producao, a causa
  exata nao foi confirmada. Candidatos no backend: asset R2 removido (painel ou
  auditoria), fonte torrent desativada/quarentenada pela identidade de pack
  (`4de76f2` reparo cross-season, `e834c9c` curadoria manual) ou modo r2_only.
  Conferir `item_sources` desses episodios antes de mudar o cliente.

## Hardening comprovado localmente — 0.12.48, 04/10/2026

- Checkout ativo: `C:/NplaySwitch/.codex-tmp/switch-access-expiry`, branch
  `codex/switch-player-hardening`, sobre ca5a7a8/.47. Nao confundir com o
  main antigo em `C:/NplaySwitch` nem editar os outros arquivos locais do usuario.
- `playback_resume_position`: completed zera a posicao de abertura nas duas
  rotas reais de main.c. Parcial conserva o dialogo e a posicao. Baseline .47
  reproduz completed=1320 iniciando em 1320; teste corrigido inicia em zero.
- NRO nao usa mais `/session/:id/fail`. Failover pede `/stream/:id` com
  exclude_source_ids, verifica descriptor completo e rejeita mesma fonte ou
  formato nao nativo. Erro local nao pode desativar uma fonte globalmente.
- GET interpreta JSON de erro/expired e failover conserva o erro de rede.
  Precheck de variantes nao para na primeira incompatibilidade.
- Legenda desejada separada da aplicada: retry 2/5/10/20s, faixa anterior
  preservada, cancel proprio e resposta so aplicada apos join se escolha igual.
- Limite unico SUBTITLE_DOWNLOAD_MAX=8MiB tambem em net.c: o guard de 4MiB
  ainda existente impedia o aumento anunciado pela .47. Corpo HTTP de erro
  tambem conta no teto. Cues continuam limitados a 200k/4MiB texto decodificado.
- Cache de legenda inclui query, rejeita truncamento e ignora so fragmento.
  Remover query podia aplicar outro idioma no mesmo caminho. Renovar assinatura
  pode exigir novo download; nao voltar a otimizar apagando identidade da faixa.
- Fala curta sobreposta nao some; extensao fora de ordem atualiza max_short.
  Regressao adicional com 200 reenvios permutados e karaokê pesado passou.
- validate_release inclui regressões de retomada, API, worker/retry/cache,
  transporte e guard do harness Linux. Testes negativos da suite.sh agora
  exigem termino correto, RESULT/SUMMARY e video real, nao apenas ausencia de erro.
- ARM64 -Werror e validacao completa local sem SkipMediaFixtures passaram.
  Nao testado em Switch fisico; suite integrada Linux nao rodada nesta sessao.
- Nenhum banco/deploy de backend alterado. Big Bang Theory T2/T3/T4 ainda exige
  IDs/trace atuais e verificacao autorizada de fontes em producao: completed
  errado e fonte global desativada sao causas distintas, nao afirmar reparo total.
- Sem Release/latest desta rodada. Relatorio, evidencias e proximo roteiro:
  `docs/PLAYER_HARDENING_0_12_48.md`. Antes de publicar, conferir git status,
  refazer build/validator, digest e teste fisico; manter NRO apenas como asset.
