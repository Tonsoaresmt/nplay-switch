# Legenda parcial e sequencia de episodios — candidato 0.12.50

## Escopo e estado

05/10/2026. NRO nativo SDL2/FFmpeg/libcurl. Checkout ativo:
`C:/NplaySwitch/.codex-tmp/switch-access-expiry`, branch
`codex/switch-player-hardening`, base c494463 (integracao 0.12.49).

Esta rodada corrige um defeito reproduzivel de legenda externa. O relato de
autoplay T3 -> progresso antigo da T5 **nao foi reproduzido nem considerado
resolvido**. Nenhuma alteracao de servidor, pacote R2 ou conta foi feita.
0.12.50 e um candidato local; nenhuma release foi publicada nesta rodada.
O console ainda recebe a release 0.12.49 pelo atualizador.

## Defeito comprovado e correcao

`load_external_subtitle_data` aceitava uma rendition HLS com qualquer quantidade
de cues, mesmo apos erro de leitura, cancelamento, erro de decoder ou falta de
memoria. Apenas VTT direto exigia EOF normal, e mesmo ele ignorava erros de
decodificacao de pacotes. A troca assincrona recebia sucesso: aplicava o texto
parcial, encerrava os retries e podia guarda-lo para o proximo seek/reopen.

Agora ambas as formas exigem EOF normal, sem cancelamento nem erro de decoder.
Abertura/fechamento de recursos da legenda usam callbacks proprios com indicador
de falha sticky **por thread e tentativa**. Isso detecta tambem segmento que o
demuxer pulou antes de devolver EOF. Callbacks da legenda nao alteram a etapa de
abertura do video. A faixa aplicada continua intacta quando a nova falha; a
falha HLS entra no retry existente (2/5/10/20 s). Texto parcial nao entra no cache.

Selecionar novamente a mesma faixa externa no painel X e confirmar com A agora
recarrega mesmo que ainda existam cues antigos. HLS continua assincrono; remux
usa o carregador cancelavel ja existente. A nova faixa so substitui a antiga
apos sucesso. Isso nao significa que toda fonte tenha legenda nem que se possa
reconstruir texto faltante no servidor.

Nao aumentar memoria indefinidamente: transporte continua limitado a 8 MiB,
texto decodificado a 4 MiB e cues a 200 mil. Exceder esses limites agora e falha
visivel/repetivel, nao sucesso truncado. Retry nao resolve um arquivo que sempre
excede o limite ou sempre esta malformado: exigir log antes de mudar os tetos.

## Evidencia e testes

`tools/test_subtitle_completion.mjs` extrai o carregador real do player e executa
o SubtitleStore real com demux/decoder/transporte simulados. Nao testa GPU,
NVTEGRA, audio ou Wi-Fi fisicos. Cobre 19 cenarios: EOF completo HLS/VTT, erro de
rede depois de cues, EAGAIN/EXIT, cancelamento tardio, teto de armazenamento,
erro de decoder, recurso pulado seguido de EOF, erro de AVIO no fechamento,
EOF normal aninhado, tentativa seguinte e 503. Verifica que erro conserva a
faixa anterior e libera os contexts/pacotes; contrato de codigo cobre recarga
manual da faixa atual.

- Base da release 0.12.49 (247610129878aaa665e4a147c1698e182f786ba9): 9 falhas
  em 19, reproduzidas por `node tools/test_subtitle_completion.mjs --baseline`.
  O exit nao zero desse comando e intencional: demonstra a regressao antiga.
- Codigo corrigido: 19/19 passaram.
- Build ARM64 completo `make -B -j4` com -Werror passou sem avisos; depois
  `make -j4` recompilou a instrumentacao final de main.c sem avisos.
- Validador completo `tools/validate_release.ps1 -SkipBuild`, sem
  `-SkipMediaFixtures`, passou nas duas rodadas, inclusive no binario final
  com a instrumentacao da sequencia. `git diff --check` passou.
- Fixtures usam FFmpeg CLI 8.1.1. NRO usa FFmpeg 7.1. Nao confundir a CLI
  com execucao do player integrado nem com validacao no aparelho.

## Autoplay e selecao na lista

`tools/test_episode_rewatch.mjs` executa seletores, choose_next_episode, loop e
input_series extraidos de main.c, com episode_flow real. API/player/render sao
stubs. A fixture tem progresso recente T5E7 e T3 inteira ja vista. Oito casos
passaram: T3E1 -> T3E2 natural, salto pelo painel e depois proximo, proximo
explicito, autoplay desligado, contagem cancelada, contexto direto/historico,
entrada A pela lista apos mudar temporada e selecao de episodio ja visto.

Nao trocar a regra de continuidade sem reproduzir o relato. O seletor inicial
do detalhe continua escolhendo o episodio inacabado com progresso mais recente,
mas o autoplay testado usa o ID do episodio realmente tocado. Reabrir o detalhe
ou usar Continuar e diferente de avancar no EOF. Precisamos da serie, T/E e logs
novos para conferir os dados reais, inclusive series agrupadas.

Novo trace seguro `episodes next`: series/current/next/group/found/autoplay/
explicit. Nenhum titulo, URL ou ID de sessao. Apos o proximo player abrir, o
evento anterior pode estar em `.nplay-player-trace.prev.log`.

## Como continuar

1. Conferir git status; nao sobrescrever outros trabalhos.
2. Se editar novamente, recompilar e repetir validador completo; hash abaixo
   corresponde somente ao estado validado desta rodada.
3. Teste fisico: mesmo anime/fontes R2 e remux, acompanhar alem do ponto onde
   sumia; pausar, seek para frente/tras, selecionar a mesma legenda no X/A,
   trocar idioma e episodio. Falha de recarga nao pode desligar faixa/video.
4. Guardar `.nplay-player-trace.log`, `.prev.log` e `.nplay-network-trace.log`;
   buscar `subtitle incomplete`, `manifest-load-fail`, `retry`, `stream-retry`,
   `session-reuse` e `episodes next`. Logs de 02/10 precedem 0.12.49 e nao
   demonstram o novo relato. Nenhuma causa especifica do episodio foi confirmada.
5. Nao considerar desaparecimento de texto sozinho prova de queda: cenas sem
   fala sao normais. Nao reiniciar video nem rebaixar automaticamente todas as
   legendas por alguns segundos sem cue. Torrent ja tem tres retries de rede;
   este trabalho nao altera essa politica nem descobre EOF prematuro do servidor
   que chegou como HTTP 200/VTT sintaticamente valido.
6. Antes de publicar, distinguir claramente validacao local de hardware. NRO
   permanece somente como artefato de build/release, nunca em commit.

Referencia primaria: [FFmpeg 7.1 HLS](https://ffmpeg.org/doxygen/7.1/hls_8c_source.html),
read_data: caminho de falha de open_input pode avancar sequencia e terminar em EOF.

Binario final candidato: 24.241.999 bytes. SHA-256:
`1894f12f48b003800a286d4517f4006918f53ffc0f27b402474d6abdd04d7560`.
