# Busca por botoes e analogico — candidata 0.12.56

## O que foi encontrado

Base local: `a4e55e9` (.55). A .55 ainda nao foi publicada nesta rodada;
clientes na .54 nao receberam o suporte a restart TorBox descrito em
`PLAYER_DELIVERY_HARDENING_0_12_55.md`. Isso pode explicar alguns casos de
busca bloqueada, mas nao identifica a fonte de cada reclamacao sem trace.

No codigo, L/R/ZL/ZR so tratavam a borda de pressionamento: segurar nao
continuava deslocando o destino. O debounce tambem publicava um A na fila
global SDL antes de drenar o input real. Uma mudanca de modal podia consumir
essa confirmacao fora do contexto pretendido. O analogico era o esquerdo,
sempre habilitado, e sua abertura nao excluia o painel de episodios/proximo.
O preview touch nao capturava a posicao atual para desenhar sua origem.

Esses sao defeitos identificados no codigo; nao ha logs recentes que provem
qual deles ocorreu no aparelho de cada cliente. Fontes sequenciais antigas
sem contrato de busca continuam tendo uma limitacao real, nao um botao quebrado.

## Comportamento implementado

- L/R somam 10s; ZL/ZR somam 60s. Segurar repete apos 450ms, em intervalos
  de 150ms, sem rajada de compensacao depois de uma pausa de processamento.
- A fonte nao e reaberta durante a escolha. Soltar permite aplicar um unico
  salto apos o debounce de 700ms desde o ultimo passo. B cancela. Abrir outra
  acao/menu cancela o salto pendente, para nao aplica-lo ao fechar o painel.
- Confirmacao automatica e local, depois dos eventos reais; nao usa
  `SDL_PushEvent`. Episodios, faixas e proximo episodio nao recebem A pendente.
  Eventos de outros joysticks nao comandam o player ativo.
- Configuracoes > Reproducao > **Analogico direito: minutagem**: desligado
  por padrao. Salvo apenas na microSD, por ID de perfil, com gravacao
  transacional existente; nenhuma preferencia nova e enviada ao backend.
  Falha de gravacao nao muda o valor da tela. Perfil sem arquivo ou com
  arquivo invalido fica desligado. Trocar de perfil recarrega seu valor.
- Quando ligado: neutralizar o stick, segurar para esquerda/direita por
  550ms e escolher o tempo. A confirma; B cancela. Soltar nao executa seek.
  Zona morta/histerese e neutralizacao apos modal evitam acionamento herdado.
  O esquerdo deixa de abrir a busca. Nenhuma leitura de eixo direito quando OFF.
- Cinco linhas de Reproducao cabem no painel e usam geometria compartilhada
  entre desenho e toque. O HUD existente conserva Confirmar/Cancelar.
- HLS/R2 usa reabertura de playlists posicionadas; MP4 Range usa a barreira
  do demux; TorBox restart exige o contrato explicito validado na .55.
  Fonte hot sequencial sem suporte recebe aviso claro, sem simular salto ou
  alterar o ponto para zero. Os limites de memoria/TLS nao mudaram.

## Fontes tecnicas

O SDL generico nao garante significado dos eixos e recomenda tolerancia a
jitter: [SDL_JoystickGetAxis](https://wiki.libsdl.org/SDL2/SDL_JoystickGetAxis).
O driver Switch da familia SDL 2.28 (SDK local 2.28.5) mapeia right-X para
eixo 2 e L/R/ZL/ZR para botoes 6..9:
[driver devkitPro](https://github.com/devkitPro/SDL/blob/0738d3c9f6993875e2f3dd0e8cc0bb4ae4440b4e/src/joystick/switch/SDL_sysjoystick.c).
Repeticao consulta o estado atual, conforme
[SDL_JoystickGetButton](https://wiki.libsdl.org/SDL2/SDL_JoystickGetButton).
Joy-Con unico horizontal e remapeado pelo proprio driver; nao prometer a
mesma disposicao fisica que o par/Pro Controller nesse modo.

## Testes e limites

`node tools/test_seek_controls.mjs` compila os helpers reais e extrai do app
o dispatcher, passos/cancelamento, ramo de configuracao e persistencia:

- quatro botoes, hold/debounce, mudanca de direcao, conflitos e wrap do clock;
- 60 passos de 60s ate 1h e retorno; destino limitado e duracao desconhecida;
- salto aplicado uma vez apos soltar; B/menu anulam o pendente;
- eventos reais primeiro; nenhum A de busca confirma faixas/episodio;
- pausa mantida, escolha sem reinicializar fonte, OFF em toda faixa do eixo;
- neutralizacao, 550ms, zona morta, sinal negativo extremo e modal;
- perfis separados, reload, OFF, dado malformado e save sem perfil rejeitado;
- cinco hitboxes de configuracao dentro de 720p, sem invadir espacos entre linhas.

UI/GPU/HTTP desse teste sao simulados. Testes de entrega existentes usam
FFmpeg CLI 8.1.1 com HTTP local (Range, remux chunked, 403 e 503); o NRO usa
FFmpeg 7.1. Nenhum deles executa NVTEGRA, servidor TorBox real ou Switch fisico.

A primeira bateria completa parou no timeout de 30s do teste de 1000 aberturas
preexistente. Instrumentacao mostrou progresso regular de ~3,1s por 100 ciclos,
900 ciclos em 28s, sem assertion falhando. O limite externo do harness passou
a 60s para acomodar a granularidade de timer Windows/MSYS. As assertions, os
1000 ciclos e os timeouts do app nao mudaram; o teste agora imprime andamento.

## Roteiro obrigatorio no console

1. Atualizar somente o NRO; abrir Reproducao e conferir opcao desligada.
2. Mover ambos sticks: nao pode buscar quando OFF. L/R/ZL/ZR continuam ativos.
3. Em R2, MP4 Range e TorBox restart: tocar, segurar, inverter e soltar os
   botoes; confirmar um salto, nao uma reabertura por repeticao.
4. Segurar ZR ate o meio, cancelar com B; ponto e pausa devem permanecer.
5. Com salto pendente, abrir faixas/Episodios/proximo: nenhuma confirmacao
   automatica nem salto tardio ao fechar. Testar outro controle conectado.
6. Ligar opcao, reiniciar app, trocar de perfil e retornar: cada perfil
   conserva sua escolha; perfil novo permanece OFF.
7. Com stick direito, abrir preview, escolher meio/fim/inicio, soltar:
   somente A deve aplicar; B e toque em Cancelar devem manter ponto original.
8. Repetir pausado e apos HOME/repouso. Em fonte sem suporte, conferir aviso
   e ausencia de reinicio em zero. Guardar trace se falhar em um titulo.

## Resultado local final — 23/10/2026

- Build ARM64 completo `make -B -j4`, seguido de recompilacao final de
  `player.c`, passou com `-Werror` e sem avisos.
- `tools/validate_release.ps1 -SkipBuild` passou no binario final, SEM
  `SkipMediaFixtures`: inclui controles novos, 1000 transicoes em 31.011ms,
  pthreads, TLS, barreira de seek, supervisor, legendas, touch, compatibilidade
  do contrato do site, fixtures reais HTTP/FFmpeg e simbolos do ELF.
- `git diff --check` passou. NRO: **24.291.151 bytes**, SHA-256:
  `79e076184974431d3a4e765205809d7174f28551ee9698efe450314acce2d4d9`.
- Nenhuma alteracao do app/rebuild apos essa bateria. Sidecar local atualizado.
- Consulta publica GitHub nesta rodada confirmou latest **v0.12.54**;
  branch remota hardening em `5ff7570`. Portanto a .55 anterior nao esta no
  atualizador e a .56 inclui tambem aquelas correcoes locais de entrega.
- Sem push/release, backend, conta de producao ou reprocessamento nesta rodada.
  Teste no console continua pendente; resultado local nao e homologacao fisica.
