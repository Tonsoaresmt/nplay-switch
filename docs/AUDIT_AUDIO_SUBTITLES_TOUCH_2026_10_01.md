# Auditoria de audio, legendas e touch — 01/10/2026

## Resultado aplicado ao NRO 0.12.36

A 0.12.30 introduziu prioridade da ultima escolha manual persistida por perfil
sobre a preferencia da conta em outras obras. Isso reproduz o relato de conta
Dublado abrindo em ingles/japones: audio_policy_choose recebia saved_language
estrangeiro e retornava essa faixa antes de procurar portugues. Os testes antigos
exigiam esse comportamento. O teste alterado falhou antes da correcao.

Agora Dublado/Legendado controla a entrada em outra obra. A persistencia local so
decide em Tanto faz. A troca manual dentro da MESMA reproducao ainda vence a conta
em seek/reconnect/reabertura, por audio_hint_priority 1/2. A versao escolhida
explicitamente no detalhe continua respeitada. Dublado nao pode criar uma faixa
portuguesa ausente da fonte; nesse caso a politica usa o fallback existente.

O cabecalho desenha B Voltar/B Cancelar a direita (right=WIN_W-50), mas o handler
de tap testava y<95 && x<260, a esquerda. Agora o renderer guarda a largura medida
da acao e ui_header_action_hit usa a mesma geometria. A correcao atende Serie,
Anime, Dorama, Filme, Saga, Perfis, Configuracoes e tela SC_LOADING. O modal de
retomada tambem reconhece Cancelar por toque. O reconhecedor continua exigindo
tap: arrastar sobre o cabecalho nao dispara retorno. B segue a origem contextual
existente; nao reinicia a busca ou manda o usuario arbitrariamente para Home.

## Conferencia dos pontos enviados por outra IA

1. **Legendas no servidor: confirmado no codigo publicado.** O commit 601785c
   existe em Tonsoaresmt/Nplay, altera hls-preparer e seu contrato, mas nao e
   ancestral de origin/main (compare divergente, behind_by=1). O main consultado
   ainda possui multitrack-v3. O ultimo deploy registrado e 4202a4d; o deploy do
   QR c650809 nao incorporou 601785c. A correcao muda o schema, rejeita reutilizacao
   sem legendas de texto esperadas e falha se nenhuma conversao funcionar. v3
   tambem PODE conter legendas: sua etiqueta, sozinha, nao prova falta em todos os
   pacotes. Subir a correcao nao atualiza automaticamente assets R2 ja prontos.
   Proximo passo: portar/revalidar no main atual e reparar uma obra afetada por
   vez, mantendo o pacote anterior ate confirmar master, VTT, timestamps e upload.
   Nenhum deploy de media, reprocessamento ou escrita no banco foi feito nesta
   auditoria. Nao disparar reprocessamento em massa.

2. **Reserva de demux: candidato confirmado, causa ainda nao demonstrada.**
   DEMUX_QUEUE_PACKETS=32 e DEMUX_QUEUE_BYTES=4 MiB. A fila mistura pacotes de
   audio/video/legenda, nao 32 quadros exclusivamente de video. Com 24-30 fps e
   AAC 48 kHz/1024 amostras, 32 pacotes podem representar aproximadamente
   0,42-0,45 s de dados intercalados. Isso depende do formato/packetizacao e nao
   deve ser apresentado como medida do filme real. high_packets/maxPackets=32
   prova saturacao do limite, nao que isso explique sozinho um engasgo: correlacionar
   fila vazia, latencia de leitura, bytes, PTS/duracao reservada e buffering no mesmo
   instante. O limite em bytes atual e um limite suave: um pacote lido pode fazer
   a ocupacao ultrapassar 4 MiB. Aumentar a quantidade exige testar memoria,
   barreira/cancelamento e gerações de seek. Nenhum aumento especulativo aplicado.

3. **Chamadas na thread SDL: confirmado.** main.c possui 15 chamadas api_send
   para favoritos, download/preparo, listas, progresso, preferencias, perfis,
   senha e email; api_send usa timeout total de 20 s. Ha tambem duas consultas de
   progresso antes de playback, com teto de 5 s. O cancelamento dessas acoes
   nao tem a mesma animacao/thread do resolvedor. Uma exclusao de favorito remove
   localmente mesmo quando DELETE remoto falha, outro defeito de consistencia.
   Implementar fila de mutacoes com payload copiado, perfil/geracao, cancelamento,
   atualizacao transacional em sucesso e tratamento de retry sem duplicar POST.
   Nao reutilizar uma struct na stack enquanto um worker ainda a acessa.

4. **Repositorio: valido, numeros enviados desatualizados.** origin/main do
   Switch continua em 0.11.0; a release sai de codex/switch-rebuild. Ha 101 commits
   locais (todos os refs) tocando Nplay.nro; count-objects mostra ~766 MiB de
   objetos soltos e ~55 MiB compactados nesta copia. Isso nao e uma medida do
   tamanho de clone remoto. O validador usa caminhos C:\\devkitPro e executaveis
   .exe, portanto nao e portavel diretamente para Linux. Consolidar branches com
   diff/revisao, parar novos commits de binario, manter NRO nas Releases e criar
   runner portavel sao melhorias reais. Nao reescrever historico nem forcar main.

5. **Fila SDL de audio: confirmado.** O caminho normal chama SDL_QueueAudio sem
   teto. Preroll ja tem limite de ~350 ms; isso nao limita a reproducao normal.
   Medir e prudente, mas a pendencia nao deve ficar indefinida. O caminho correto
   e contrapressao com clocks/controles responsivos; jogar fora amostras ou limpar
   a fila quando ela cresce causaria silencio e perda de sincronismo.

## Limites da verificacao de legendas

O NRO lista streams nativos e possui fallback EXT-X-MEDIA/WebVTT. O fallback so
entra se native_nsub==0; nao completa uma lista parcialmente descoberta. A carga
externa da legenda ainda e sincrona e decodifica o arquivo em uma colecao limitada
na thread do player. Isso tambem merece teste de rede lenta/cancelamento.

O fixture local abriu cinco streams e duas legendas; nao executa o driver ARM64,
NVTEGRA, SDL de audio ou o conteudo real fotografado. Para o caso em que PC mostra
legendas e Switch nao, comparar o MESMO item/source e manifesto efetivo. Observar
hls-master/renditions, streams/enumerated, subtitle/manifest-fallback e
subtitle/first-cue. O PC pode usar extracao externa, ausente no descritor nativo;
menus parecidos nao garantem que ambos estejam recebendo a mesma fonte/pipeline.

Fontes oficiais consultadas:

- https://wiki.libsdl.org/SDL2/SDL_QueueAudio — fila sem limite automatico; falta
  de dados produz silencio.
- https://wiki.libsdl.org/SDL2/SDL_GetQueuedAudioSize — bytes ainda nao enviados
  ao hardware; nao fornece a fronteira exata do que ja foi ouvido.
- https://datatracker.ietf.org/doc/html/rfc8216#section-4.3.4.1 — renditions HLS
  anunciadas por EXT-X-MEDIA, idioma e URI das legendas.

## Validacao concluida

- Regressao antes da correcao: teste conta Dublado + ingles persistido falhou.
- Depois: suite validate_release.ps1 completa, build limpo ARM64 -Werror,
  audio/continuidade, touch (tap/jitter/arraste/ownership/cabecalho), clock, sync,
  loader, proximo episodio, API, episodios, HLS/WebVTT e remux passaram.
- Fixture HLS: 5 streams, 2 cues, decode no inicio e depois de seek.
- NRO 0.12.36: 24.184.655 bytes; SHA-256
  8f78205f56a2380644b0876a7bab92363af0037c222ca1f10b8f9ed445361b73.
- Pendente fisico: touch Voltar em Serie/Anime/Dorama vindo de Home e busca,
  cancelar retomada, Dublado com lingua antiga persistida, selecao manual e
  reconnect na mesma reproducao; comparar disponibilidade de legendas da obra real.
