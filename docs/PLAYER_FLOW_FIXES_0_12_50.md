# Consolidacao de fluxos do player — candidato 0.12.50

Rodada de 05/10/2026, sobre e6e183f. Checkout ativo:
`C:/NplaySwitch/.codex-tmp/switch-access-expiry`, branch
`codex/switch-player-hardening`. Apenas o NRO SDL2/FFmpeg/libcurl foi alterado.
Servidor, conta, R2 e releases publicas nao foram modificados.

## Resultado implementado

| Falha da auditoria | Mudanca | Limite |
| --- | --- | --- |
| EOF prematuro concluia a obra | Drena video/audio atrasados; confere posicao contra duracao e erro de I/O; fim incompleto retorna -5, nunca autoplay. Finalizador tem segundo guard. | Duracao desconhecida ainda aceita EOF limpo; metadado incorreto pode provocar recuperacao conservadora. |
| Historico mudava alvo ao atualizar | Menu captura item_id; refresh restaura selecao por ID; alvo removido fecha o menu. | API deve identificar cada obra com ID unico. |
| Pausa perdida ao reabrir | PlayerRequest transporta start_paused na mesma reproducao. Pausado pode decodificar o primeiro quadro/preroll, sem liberar audio, e depois congela. | Pipeline completa/NVTEGRA ainda precisa de hardware. Nova obra nao herda pausa. |
| Touch nao funcionava nos modais | Episodios tem toque, rolagem vertical e Fechar; timeline tem preview por arrasto, Confirmar e Cancelar, com geometria compartilhada pelo desenho. | Sem validacao visual/pixel ou toque no console. |
| Join de legenda bloqueava a tela | Substituicao/cancelamento do fetch R2 retorna sem esperar. Um worker + um pedido pendente, ultimo vence; so reutiliza dados depois do join do worker concluido. | Hot/progressivo e teardown completo ainda possuem waits; nao declarar todo join eliminado. |
| Abas perdiam contexto | Snapshot limitado por aba preserva rail, item/hero por ID e scroll. Historico/lista/biblioteca conservam subvista; menu antigo nao e reaberto. Scroll vertical/horizontal e limitado no desenho. | Snapshots locais ao processo, nao persistidos ao reiniciar. |
| JSON de Historico apagava dados | Sem items array, preserva o estado conhecido e informa indisponibilidade. Array vazio valido e aceito. | Falha de transporte continua usando os caminhos existentes. |
| Watchlater nao refletia exclusao remota | Reconciliacao transacional em memoria, por ID + tipo. Itens confirmados ausentes sao removidos; adicoes locais/legadas sem origem confirmada ficam. Geracao impede GET antigo desfazer POST/DELETE. | Migracao conservadora pode manter fantasmas antigos ate confirmacao. Teto continua 64; exceder preserva lista anterior e avisa. |
| Pedido de aba antigo continuava na fila | Entrar em cache limpa g_land_queued_tab. Pedido ja em voo pode concluir e preencher cache, mas nao substitui aba atual. | Nao cancela agressivamente um HTTP em voo. |

Toque nos botoes +/-10s usa o mesmo coalescimento dos botoes L/R, em vez de
executar o seek sincrono separado a cada toque. Arrastar/soltar a barra apenas
prepara o alvo; Confirmar executa uma unica operacao. Um gesto pertence a um
dedo; outro dedo, gaps entre linhas e controles escondidos nao disparam acoes.

O evento `demux/read-terminal` agora inclui pos/dur/ioFailed/drained, sem
URLs ou credenciais. O flag de falha HLS pertence a tentativa e e atomico.
Ele e conservador: se um recurso falhou/saltou, nao prova que o EOF representa
todo o video. Uma nova tentativa cria seu proprio estado, sem herdar esse erro.

## Evidencias de teste (nao confundir com Switch real)

1. `test_player_flow_guards.mjs`: extrai funcoes C reais do Historico, finalizador
   e decisao terminal. Simula reorder, exclusao, JSON invalido/vazio, snapshot
   watchlater tardio, EOF aos 5min de uma obra de 30min, EOF normal, I/O falho,
   duracao desconhecida e gate de pausa/preroll. Rede, decoder e SDL simulados.
2. `test_player_supervisor.mjs`: player_run real com pipeline roteirizada;
   preservacao de posicao e pausa em seek, audio e recuperacao; nova reproducao
   inicia sem pausa residual. Nao decodifica video real.
3. `test_player_navigation.mjs`: C real de landing_apply/load/enter_tab e gestos
   dos modais; IDs reordenados, aba em cache com pedido antigo, contexto de
   Historico, dedo secundario, swipe, preview, confirmacao e areas inativas.
   Renderer e eventos fisicos simulados; sem screenshots de hardware.
4. `test_subtitle_fetch.mjs`: worker pthread real, com atraso artificial de
   300ms. Replacement/off retornam em menos de 100ms no host; texto anterior
   permanece, resultado cancelado nao e aplicado e falha de criar o worker
   pendente volta ao retry. Isso NAO mede latencia do Wi-Fi do Switch.
5. `test_media_list_sync.mjs`: modulo de merge e wrappers C reais com cJSON;
   vazia/invalida, ID+tipo, confirmacao/exclusao, local offline, 64 itens,
   capacidade excedida e falha injetada em cada alocacao. Save para microSD
   e mock: nao comprova resiliencia de escrita no SD; mecanismo atomico
   preexistente de arquivo foi preservado.
6. `audit_player_flows.mjs --baseline` continua reproduzindo os defeitos de
   e6e183f. Exit 0 ali significa DEFEITOS REPRODUZIDOS, nao versao aprovada.
   Sem --baseline recusa executar; nao entra no validador positivo.

As suites novas foram incorporadas em `tools/validate_release.ps1`. Tres
rodadas completas passaram: duas antes do ultimo ajuste de contexto do
Historico e a terceira sobre o binario final, com esse ajuste incluido.
As suites existentes de concorrencia, transporte real libcurl, legendas,
audio policy, sequencia/rewatch, contrato local com backend e fixtures FFmpeg
nao foram removidas nem tiveram assertions enfraquecidas.

## Referencias oficiais utilizadas

- [FFmpeg 7.1 — send/receive e draining](https://ffmpeg.org/doxygen/7.1/group__lavc__encdec.html):
  enviar NULL no EOF para receber quadros atrasados. Aqui a nova decisao e
  simulada; o drain com codecs/NVTEGRA ainda precisa de teste integrado.
- [SDL_WaitThread](https://wiki.libsdl.org/SDL2/SDL_WaitThread): o join espera o
  worker e invalida seu handle. Dados compartilhados nao podem ser zerados ou
  reutilizados antes disso; pedido pendente fica separado do worker ativo.

## Build e artefato

- `make -B -j4` completo ARM64 passou com -Werror. A ultima mudanca em main.c
  (contexto do Historico) foi recompilada e relinkada por `make -j4`, sem avisos.
- Candidato local 0.12.50, sem release/push/deploy nesta rodada.
- NRO ignorado pelo Git: `Nplay.nro` no checkout ativo. Nao commitar binario.
- Artefato final: 24.258.383 bytes; SHA-256
  `8bee05c3ebb5b4bd45ad2dec8f9a55ca75354e8ac79299df3d08d83d0a3bb5f3`.
- Validacao final: `tools/validate_release.ps1 -SkipBuild`, exit 0, SEM
  `SkipMediaFixtures`, confirmou o tamanho e SHA acima. Contrato com checkout
  LOCAL do backend, nao producao. `git diff --check` passou.

## O que ainda nao esta comprovado / proximos passos

- Switch fisico: pausa longa, seek pausado, 20 toques rapidos L/R/touch,
  arrasto ate metade e confirmacao/cancelamento; audio/legendas com rede ruim,
  fim de episodio com B-frames e playback de mais de 60min. Registrar traces
  novos, titulo, T/E, versao e posicao exata. Testar toque/scroll/overscan720p.
- Linux integrado `tools/host_player`: nao rodado nesta rodada; necessita SDL2
  e FFmpeg7.1 no host. Fixtures do validador usam FFmpeg CLI8.1.1 neste Windows;
  nao equivalem a testar player.c completo com o decoder do Switch.
- Refatorar ownership de legenda hot/progressiva e teardown sem wait na UI,
  mantendo cancel proprio e join antes de liberar memoria. Nao destacar thread,
  nao usar demux_abort para cancelar so texto, nao deixar jobs ilimitados.
- EOF HLS: flag de I/O e guard por duracao nao sao prova de todos os segmentos.
  Verificar no trace se a politica conservadora reabre indevidamente um video
  que ja chegou ao fim apos uma falha antiga recuperada. Nao remover guard
  simplesmente para fazer um teste passar; melhorar a classificacao do recurso.
- VTT HTTP200 truncado mas sintaticamente valido continua sem prova de
  completude sem marcador/contrato do servidor. Nao recarregar por silencio
  normal de dialogo. Nao prometer paridade ASS/graficos/bitmap do navegador.
- O incidente T3->T5 nao foi reproduzido: 8 cenarios reais de seletores/loop
  continuam passando. Esta rodada nao deve ser anunciada como causa conclusiva
  do incidente, nem do buffering de 50min, sem os logs correspondentes.
- Nao publicar como estabilidade comprovada no hardware. Primeiro conferir
  localmente todas as suites e o hash do artefato, depois teste controlado no
  console; se publicar candidata, rotular limites/testes pendentes explicitamente.
