# Animes: fonte R2, legendas e próximo episódio — 03/10/2026

## O que foi observado, e o que não foi comprovado

Usuário abriu **Super no Ura de Yani Suu Futari** e relatou várias legendas no
navegador, mas nenhuma no NRO. Não forneceu ainda número de episódio ou trace
dessa tentativa. Não atribuir a Animes Drive só porque o painel não lista faixas.

Backend `origin/main` a25a576 foi consultado no GitHub. `allActiveSources` coloca
fontes managed/R2 antes de embeds/Animes Drive, tanto com `torrent_capable` quanto
sem. NRO usa o POST `/api/stream/:itemId` sem forçar provedor; respeita delivery e
container da resposta. A consulta pública de catálogo respondeu 401. Não usar
credenciais de terceiros nem criar sessão de teste como substituto do trace.

Isso verifica a regra no código, não os registros/artefatos deste episódio em
produção ou qual commit está implantado no servidor. Possíveis diferenças ainda
a investigar: ID/versão diferente do episódio, fonte R2 não publicada/ativa,
pacote sem renditions ou falha de descoberta/leitura das renditions no NRO.
Também não há migração automática de uma reprodução upstream já iniciada para
R2 quando o preparo termina, como há no navegador. Abrir de novo resolve a fonte
novamente; não introduzir troca ao vivo sem preservar sessão, ponto e faixas.

Logs antigos do usuário em Temp são de 0.12.38, item=2, delivery=r2; master
anuncia quatro legendas e fallback registra quatro. São de OUTRA tentativa,
não provam nem refutam o problema no anime citado.

## Alterações desta rodada

- Cartão de próximo episódio aparece ao pausar ou fixar HUD, além da janela final
  de 45 s. Direita seleciona, A confirma; B/esquerda cancela sem sair do episódio.
  Toque no cartão seleciona primeiro, segundo toque confirma (evita toque
  acidental iniciar outro vídeo). Não existe detecção da posição real dos créditos.
- Sequência consulta a ordem T/E do detalhe inteiro, não apenas `ser_nep()` da
  temporada visível. Último episódio de temporada pode oferecer a próxima;
  finale sem sucessor não anuncia ação. Temporada agrupada mantém a intenção
  explícita mesmo se autoplay estiver desligado e houver consulta assíncrona.
- Home/Histórico com `series_id` consultam contexto via worker UI cancelável
  (2 s conexão, 5 s total) quando o detalhe ainda não está carregado. Falha não
  inventa sucessor. Cancelamento não abre o player. Nenhuma consulta por frame.
- Fallback de preparo conserva `subtitle/overview/next_title/has_next` por
  apresentação emprestada somente durante a chamada síncrona; o player fecha e
  faz join antes de retornar. Resultado NEXT continua sendo 2, distinto de EOF.
- Diagnóstico registra provedor allowlisted `animesdrive/hinatasoul`, seção e
  T/E, sem URL/token/SID textual. O cabeçalho continua mostrando delivery R2.
  Provedor ausente fica unspecified: não inferir pela extensão/host do vídeo.
- Não alterou empacotador, seleção de áudio, decoder de legenda ou fontes de
  produção. Não dizer que esta rodada corrigiu legendas do anime.

## Verificação

`tools/test_episode_sequence.mjs` extrai/executa a função real de main.c com
API/player simulados. Baseline b283f72 falha em has_next na mesma fixture;
atual passa ordem cronológica, temporadas não ordenadas, final, grupo,
abertura direta, cancelar consulta e escolha explícita. Não simula SDL físico.

`test_playback_source.c` executa resolver API real com HTTP fake: anime R2/HLS,
Animes Drive MP4, descriptor sem resíduo e rótulo secreto rejeitado. Não é
integração de produção nem prova que todas as renditions reais funcionam.

Build ARM64 completo com `make -B -j4` e `-Wall -Wextra -Werror` passou.
`validate_release.ps1 -SkipBuild` completo passou SEM SkipMediaFixtures:
worker pthread, memória, curl real/espera local, supervisor/checkpoint, seek,
áudio, touch, fluxo de episódios, HLS multifaixa/seek e WebVTT/remux reais.
Não foram executados no Switch físico. Publicação deve ser registrada depois
de confirmar que o asset corresponde ao build final.

Binário final: 24.205.135 bytes; SHA-256
`9321a245ef44b7a679781480a08b51c8978c28eb6246e57f201651aef046de55`.

## Próximos passos / hardware

1. Confirmar versão e episódio exato; após tentativa subir
   `sdmc:/switch/.nplay-player-trace.log` sem abrir outro vídeo antes.
2. Conferir cabeçalho delivery e evento source/resolved; master renditions,
   streams enumerated, manifest-fallback e falhas de legenda. Zero AVStreams
   sozinho não significa zero legendas: há fallback do master.
3. Comparar MESMO item/source_id/manifesto com navegador, via credencial de teste
   autorizada. Conferir publicação R2 e referências WebVTT antes de reprocessar.
4. Hardware: anime via R2 e upstream, X > Português, seek com cues, pausa >
   Direita/A, B cancela, toque duplo, troca de temporada, autoplay desligado,
   último episódio. Ainda obrigatório; não declarar paridade total.

Checkout C:/NplaySwitch/.codex-tmp/switch-access-expiry, branch local
codex/switch-access-expiry, destino de publicação codex/switch-rebuild.
