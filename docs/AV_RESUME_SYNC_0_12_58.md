# Audio/video apos Continuar e salto — candidata local 0.12.58

## Relato e alcance da conclusao

Em30/10/2026 o usuario relatou imagem/som dessincronizados ao continuar ou
avancar em **The Big Bang Theory**, com o proximo episodio normal. So o titulo
foi confirmado; faltam temporada/episodio, versao instalada e trace recente.
Base: `ad7f3e4`, app publicado .57. Nenhuma conta/producao foi consultada.

Dois defeitos do **codigo real da .57** foram reproduzidos antes da correcao:

1. O limite do preroll permitia enfileirar cerca de0,35s e depois descartava
   PCM ja decodificado enquanto aguardava o video. Ao concatenar o proximo
   bloco, SDL nao recupera os timestamps perdidos. No teste de2s, a .57
   guardou0,36s e criou um buraco de1,64s; `audio_clock - queued_seconds`
   passou a indicar um tempo diferente do inicio efetivamente enfileirado.
2. O filtro de audio antigo dependia exclusivamente de `resume_preroll`,
   desativado ao chegar o primeiro quadro. Um bloco em2999s entregue depois
   do quadro-alvo3000s era aceito e tocava atrasado. Na regressao antiga,
   entrou0,1s desse audio; a correcao o rejeita/corta conforme as amostras.

Isso comprova defeitos capazes de produzir o sintoma; **nao comprova qual
deles ocorreu no episodio especifico de TBBT sem o trace desse incidente**.

## Mudancas locais

- `PlayerAudioTimeline` conserva um piso de PTS por tentativa/seek, inclusive
  depois do primeiro quadro. Cortes sao em amostras S16 interleaved, nao em
  pacotes inteiros com tolerancia de150ms. Sobreposicoes/duplicatas sao
  aparadas para o fim da fila; PTS ausente usa continuidade inferida.
- O atraso interno do resampler e descontado do inicio do bloco convertido.
  Seek confirmado reinicializa a linha do tempo e libera o resampler antigo;
  pausa/menu sem seek NAO apagam o PTS final do audio que ainda esta na fila.
- Em vez de decodificar e perder audio acima do prebuffer, o agendamento busca
  video e conserva audio comprimido na fila demux existente. Backpressure
  em350ms durante preroll/1000ms tocando; EOF libera o caminho normal/drain.
  A consulta de terminal usa lock curto/O(1), nao varre512 pacotes por quadro.
- Fila PCM limitada a4s (~768KB em48kHz/stereo/S16). Pequenas lacunas de PTS
  recebem silencio com um bloco constante de4KiB. Lacuna>2s ou teto excedido
  falha explicitamente e usa a recuperacao existente, sem falsear alinhamento
  nem recomecar em zero. Falha SDL mantem retorno de erro, nao sucesso.
- O video do preroll avanca ate o alvo (tolerancia1ms, nao150ms). Som so e
  liberado depois do novo `SDL_RenderPresent`; cancelar menus/timeline,
  tocar pausa ou despausar durante preroll nao contorna esse gate.
- Trace registra `sync anchor` com alvo/video/audioHead/audioEnd/origem/fila,
  `audio timeline-rejected` e um resumo por tentativa de trim/padding/rejeicao.
  Nao registra URL, identificador hot, credencial ou conteudo da conta.

## Evidencia e testes

`node tools/test_av_resume.mjs --baseline-057` e a variante `--late` sao
testes negativos: devem falhar. **Exit0 de uma reproducao negativa nunca e
criterio positivo de release.** O bloco C antigo e obtido de `3f0bae7`.

Sem baseline, o teste extrai do player o helper de fila PCM, o bloco efetivo
de aceitacao de audio e o agendador de pacotes. SDL/resampler/tempo sao
simulados. Cobertura positiva inclui:

- Continuidade dos2s de preroll; audio antigo apos o video; corte parcial e
  sobreposicao; piso do seek mantido; origem de timestamp diferente de zero.
- 400 epocas de seek e40.000 blocos; voltar para zero,600s e3000s.
- Falta de PTS, atraso do resampler, padding, falha SDL, rejeicao por lacuna
  e teto; device pausado/nao apresentado/em preroll; preservacao em menus.
- Audio comprimido antes do video nao e removido para abrir espaco PCM;
  video e escolhido da reserva, sem perda da ordem das demais faixas;
  terminal/EOF nao fica preso nesse gate.

`node tools/test_av_pts_fixture.mjs` gera4 MP4s H264/AAC sinteticos, decodifica
e reamostra audio com FFmpeg real. Os1087 blocos/timestamps reais alimentam
o helper/bloco C do app com SDL simulado:44,1/48kHz, origem0/37s, atraso de
audio0/120ms e alvo2,5s. Nao executa o NRO nem o dispositivo de som fisico;
a origem e conhecida da fixture, nao testa a descoberta dessa origem em HLS.

As suites anteriores de demux pthread, barreiras, retomada, legendas, controles,
HOME, TLS e55 cenarios de midia continuam obrigatorias no validador. CLI usa
FFmpeg8.1.1; SDK/NRO7.1. O teste isolado de fluxo passou apos uma primeira
invocacao do compilador falhar sem stderr; nenhuma causa foi presumida.

## Fontes oficiais

SDL copia os bytes na ordem em que sao enfileirados e nao possui timestamps
nessa API: [SDL_QueueAudio](https://wiki.libsdl.org/SDL2/SDL_QueueAudio).
O tamanho ainda nao enviado ao hardware nao mede a posicao audivel exata:
[SDL_GetQueuedAudioSize](https://wiki.libsdl.org/SDL2/SDL_GetQueuedAudioSize).
O resampler pode reter amostras e expoe esse atraso por `swr_get_delay`:
[FFmpeg7.1 swresample](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libswresample/swresample.h).
Por isso o ajuste suave do relogio de video preexistente nao foi substituido
por reposicionamentos bruscos baseados apenas nos bytes SDL.

## Limites/pontos pendentes

- Nao houve Switch/NVTEGRA, rede real, sessao TBBT, conta/backend/R2/TorBox
  de producao ou sessao longa. Nao prometer que o episodio relatado foi resolvido.
- Lacunas legitimas maiores que2s e audio extremamente adiantado precisam
  de avaliacao especifica; agora causam recuperacao explicita, nao sincronismo
  presumido. Fila comprimida permanece512 slots/4MiB (12MiB sequencial).
- Origem de HLS posicionado sem abertura inicial ainda depende do contrato
  existente do pacote. O caso de timestamps nao zero com origem desconhecida
  nao foi homologado. Os novos anchors ajudam a identifica-lo no trace.
- API de device SDL nao informa a latencia exata da saida fisica. Invariantes
  da fila em simulacao nao homologam sincronismo labial no hardware.

## Prova final / roteiro do aparelho

Build ARM64 completo `make -B -j4` e incrementais finais passaram com `-Werror`.
Bateria FINAL `tools/validate_release.ps1 -SkipBuild` passou completa, SEM
SkipMediaFixtures, incluindo novos testes A/V, 55 cenarios de midia, fila
demux pthread, legendas, controles, retomada/HOME, TLS e simbolos do ELF.
Nenhuma mudanca do app/rebuild apos essa bateria.

NRO final: **24.299.343 bytes**, SHA-256:
`9a7b052875d6fdc5ddac96b4bdccf39b10ecc9955b13a09109fe316a320c3cab`.
`git diff --check` passou. Sidecar local atualizado; binarios nao commitados.
Sem publicacao autorizada nesta rodada; a release publica .57 nao foi alterada.

No console: abrir o MESMO episodio de TBBT do inicio e por Continuar,
avancar10/60s, ir ao meio, voltar, testar pausado/tocando e abrir/fechar faixas
durante a retomada. Comparar dialogo/labios por pelo menos2min apos cada acao;
testar episodio seguinte e HOME/repouso. Capturar versao, T/E e arquivos novos
`nplay-player-trace.log`/`nplay-network-trace.log`, antes de abrir outra obra.
