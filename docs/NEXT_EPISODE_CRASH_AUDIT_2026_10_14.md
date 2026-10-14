# Falha na passagem de episodio — 14/10/2026

## Resultado e limites

O usuario confirmou que o fechamento aconteceu ao passar para o proximo
episodio. Os arquivos enviados identificam NRO 0.12.53 em modo application.
Eles demonstram falha de resolucao do episodio seguinte, NAO a instrucao,
excecao ou causa nativa do fechamento. Nao foi fornecido crash report do
Atmosphere. O crash fisico nao foi reproduzido neste ambiente.

Correcoes defensivas e diagnostico implementados em candidato LOCAL 0.12.54.
Nao publicado, sem deploy, acesso a contas, reprocessamento de pacotes ou banco.
Arquitetura SDL2/FFmpeg/libcurl, TLS e tetos de midia preservados.

## Evidencia dos arquivos

Arquivos analisados: player-trace.prev, player-trace (1), network-trace,
player-boot e nplay-ca.pem. Horario 08:04:38 dos arquivos e horario da copia;
os ticks dos logs sao relativos ao processo, nao datas UTC do incidente.

- Rede: 790 registros; 754 HTTP 200, 34 retornos -28, dois -42. Nao ha HTTP
  401/403/5xx, libcurl 60/77 ou importacao SSL falhada nestes arquivos.
- -28 = timeout libcurl; -42 = cancelamento por callback. No encerramento do
  segundo episodio, -42 corresponde ao cancel_io deliberado apos grace 1200ms.
  Nao confundir esse cancelamento com perda espontanea de sinal.
- Os cinco pedidos de fonte bem-sucedidos demoraram 7,057–8,605 s. O sexto
  atingiu 20,002 s sem corpo. Esses dados nao separam Wi-Fi/DNS/handshake/API/
  resolvedor upstream; os tempos por fase antigos nao foram registrados.
- Traces preservados sao ANIME/MP4 do provedor allowlisted hinatasoul, nao
  The Big Bang Theory nem entrega R2. DELIVERY_UNKNOWN significa que esse
  descriptor nao trouxe o campo, nao que o video nao tocou.
- Primeiro trace: item129316, 32.151 quadros, zero descartes, dois waits,
  pior343ms, sem erro; pausa128,425s e quatro seeks concluídos. Pedido manual
  de proximo em1418,97s de1440,21s; retorno4 e next129621.
- Segundo: item129621, 33.318 quadros, zero descartes, um wait313ms, sem erro.
  Seek1378,81 ->1407,43 retomou em1407,28, apos173quadros de preroll. Pedido
  manual de proximo em1410,75s; retorno4 e next228916, serie7272.
- Nao aparece run-begin para228916. O ultimo I/O e POST/stream/228916 timeout.
  Nao ha evidencia de que o decoder de228916 chegou a ser criado.
- Heap/free dos traces nao mostra OOM. proc3185MB e reserva de processo,
  NAO a quantidade usada pelo malloc. O boot09 era um breadcrumb de reproducao
  antigo: o codigo nao o atualizava ao resolver o episodio seguinte.
- Audio: zero underruns/falhas nos dois resumos. AVStreams encontrados eram
  audio+video; selected0/packets0/cues0 nao revela a disponibilidade de tracks
  alternativas no servidor nem prova que a falta de legenda causou o crash.
- CA enviada: 188900B, SHA256
  f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9,
  identica a data/cacert.bin. Nao justificaria desabilitar TLS ou trocar CA.

### Ultima transicao registrada

| Tick (ms) | Etapa | Resultado |
| --- | --- | --- |
| 7.182.205 | usuario pede proximo | item129621,1410,75s |
| ~7.183.981 | player encerra e escolhe proximo | retorno4,next228916 |
| ~7.183.988 | progresso e fonte de228916 iniciados em paralelo | inferido por fim menos duracao |
| 7.188.989 | GET/progress/228916 | -28,5001ms,0B |
| 7.203.990 | POST/stream/228916 | -28,20002ms,0B |

O limite observado e ANTES de novo player_run. Ha quatro possiveis areas
ainda nao distinguidas: transporte/SSL nativo no worker; join/finalizacao;
retorno a UI/capas/GPU; outro componente do ambiente homebrew. Timeout por
si so nao deveria encerrar o software. Nao declarar nenhuma como causa raiz.

## Defeito comprovado e correcoes

### Progresso desconhecido nao significa inicio

Na publicada .53, resolve_progress_thread marcava progress_fetched=true
sempre que nao houve cancel, inclusive quando GET expirou e retornou NULL.
resolve_and_play_resolved entao podia abrir desde zero e posteriormente
substituir o ponto salvo. Esse defeito foi reproduzido com a funcao publicada,
independentemente do crash. No incidente final, /stream tambem falhou; logo
nao houve prova de sobrescrita de progresso naquele pedido.

Agora so aceita {progress:null} ou objeto com posicao finita/nao negativa e
completed boolean/0/1 quando presente. Em falha faz uma consulta GET cancelavel
de fallback. Se continuar desconhecido/invalido, nao entra no player, tenta
encerrar a sessao alocada atraves da UI assincrona e avisa; nao inventa zero.
Validacao restrita ao fluxo API-resolved. Caminho legado/local da Biblioteca
nao foi refatorado nesta rodada. Stop pode falhar com rede indisponivel; nao
foi implementada outbox duravel de progresso/stop.

### Falha ao abrir proximo

A fonte falhada oferece A/Tentar novamente ou B/Voltar, tambem por toque.
O novo POST so ocorre apos confirmacao; nao repetir automaticamente uma
mutacao cuja resposta pode ter sido perdida. Acesso expired retorna ao login.
Ambos workers anteriores sao joined antes de reutilizar ResolvePoll e antes
de mostrar a escolha. Uma tentativa de source continua limitada a20s.
Isso melhora o caminho de falha, mas nao e prova de correcao do crash nativo.

### Memoria e diagnostico

- Descriptor /stream limitado a64KiB no callback real de crescimento, inclusive
  resposta chunked sem Content-Length. Nao amplia ring/fila/video/legendas.
- Trace resolve/begin e joined ocorre antes de player_run, com item/rc/cancel,
  sem URL, titulo, JWT ou SID textual. O boot informa resolucao/falha/espera.
- finish agora vem depois de pipoca/pool/carry/subtitle-session liberados.
  release-begin distingue esse trecho; boot10 indica encerramento concluido.
- Apos primeiro Present do catalogo, ui/playback-return-present e boot11
  confirmam retorno ao desenho. Marcadores esparsos, nao por quadro.
- Rede registra dns/tcp/tls/first em ms cumulativos a partir da requisicao.
  Sao floats tradicionais da SDKcurl7.69 convertidos de modo bounded, em TLS
  thread-local e consumidos uma vez. Zero significa etapa sem medida, nao
  necessariamente latencia zero. Sem ERRORBUFFER bruto, que pode expor URL.

## Validacao executada

- Build ARM64 completo make -B -j4, -Werror, seguido de incremental FINAL.
- tools/test_playback_resolution.mjs: workers/main/API C reais, pthreads,
  HTTP/SDL simulados. Timeout, sucesso, retry confirmado, B/touch/quit, threads indisponiveis,
  expired, schema, guarda real de entrada, 1000 transicoes sem threads/JSON
  pendentes, callback HTTP real64KiB e timings em4threads/40000consumos.
- --baseline-progress extrai resolve_progress_thread de3d36eb6/.53: exit1
  esperado na assertion que rejeita progress_fetched=true para timeout/NULL.
  Nao incorporar esse modo negativo como teste verde da release.
- tools/validate_release.ps1 -SkipBuild, SEM SkipMediaFixtures: PASSOU duas vezes no
  artefato FINAL. Snapshot de contrato backend: build/site-c9d0f77 (sem mudar
  servidor). Inclui buffer100k, demux pthread, supervisor, navegacao/cache,
  TLS real+servico nativo fake, libcurl localhost/backpressure, API/hot,
  legendas, ordem/rewatch, fixtures HLS/WebVTT/remux e simbolos ELF.
- git diff --check passou. CA/digest final conferidos.

NRO local .54:24270671B, SHA256
e937e2aed408c290e35fe217af366f251734b264bc2adecd9323d46c7c144da1.
Nao houve rebuild do candidato.54 apos a validacao FINAL. Sidecar atualizado;
NRO nao commitado.

Nao executado: Switch/GPU/NVTEGRA/SSL Horizon reais, incidente de sistema,
reproducao remota do item228916, sessao longa ou sono. Testes simulados usam
esperas reduzidas, nao demonstram20s de escalonamento Horizon. Nao publicar
como crash corrigido/estabilidade definitiva.

## Para concluir a causa do fechamento

1. Obter foto/codigo do erro e crash_report/fatal_report correspondente em
   atmosphere/crash_reports ou atmosphere/fatal_reports. Nenhum foi recebido.
2. Manter ELF do candidato/publicada correspondente: resolver PC/backtrace
   com addr2line; identificar processo e thread que falharam, distinguindo
   app, driver/servico e homebrew externo. Nao simbolizar dump.53 com ELF.54.
3. Em teste manual autorizado: repetir proximo, deixar Wi-Fi indisponivel
   nessa transicao, esperar20s, verificar card/A/B/touch e voltar a serie.
4. Copiar logs imediatamente, antes de iniciar outro video, e fotografar erro.
   Os marcadores permitem separar espera HTTP, cleanup e retorno grafico.
5. Correlacionar erro com metricas do servidor autenticado apenas quando o
   usuario autorizar/acesso normal existir. Nao procurar credenciais em logs.

O problema de sessao R2 encerrada apos hibernacao permanece separado, descrito
em SLEEP_RESUME_AUDIT_2026_10_13.md; os novos logs de anime nao o comprovam.

### Simbolos da versao do incidente preservados

Reconstrucao isolada de3d36eb6/.53 em build/crash-symbols-0.12.53/Nplay.elf,
com SDK e libavformat locais; nao altera checkout nem candidato.54.
Comparado byte a byte com build/public-release-0.12.53.nro, cujo digest e o
publicado8d877c7463f58649454353899535b94acb5e0afc087bdf136684019614cba5b7.

Mesmo tamanho/layout de secoes; somente40bytes diferem:20do ModuleId no
header0x40..0x53 e os mesmos20do GNU build-id no offset23429800..23429819.
readelf confirma .note.gnu.build-id VA0x1658298 e prefixo NOTE/GNU. Codigo,
demais dados e assets identicos. Assim os enderecos de codigo sao compativeis
para addr2line do dump.53. NAO afirmar que o novo NRO.53 tem SHA identico:
ele tem digestd46591d6f6bcbbe5c4cdd998b3d99aa083433c1d31d3bec0428e2d9e3994ca68
e ModuleId34db86dd67ebf4ded38751c468f786f902e5ea440 em vez de
4ddab6f8ce87e1f62988634097aa73ed453d1d2d0 do publicado. Nao publicar essa copia,
nao remendar IDs para fingir reproducibilidade, nem usa-la para dump.54.

## Referencias primarias

- https://curl.se/libcurl/c/libcurl-errors.html (28 timeout;42 callback)
- https://curl.se/libcurl/c/curl_easy_getinfo.html (timings tradicionais)
- https://wiki.libsdl.org/SDL2/SDL_WaitThread (join/lifetime)
- https://switchbrew.org/wiki/NRO (ModuleId/GNU build-id e secoes)
