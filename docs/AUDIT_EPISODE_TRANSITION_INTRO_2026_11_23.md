# Passagem de episodio e Pular abertura — auditoria do NRO

## Escopo e estado conferido

Pedido de 23/11/2026: verificar atualizacoes, fechamento intermitente do video
ao trocar de episodio e posicao do botao Pular abertura. Esta rodada altera
somente testes, ferramenta host e documentacao: **nao modifica o app, nao gera
novo NRO e nao publica release/deploy**.

- Switch local/remoto: `3b3184e`, .60 publicada sobre `2ebb064`.
- Site atual e health publico: `1e59abdf37648141b52a6fe6c1adf9574731da64`.
  Esse commit inclui `45a96859`: a correcao QR antes enfileirada agora esta no
  historico da API em producao. Nao foi feito deploy nesta auditoria.
- Nao havia novos logs do incidente nos caminhos antigos dos anexos. Sem obra,
  episodio e trace da tentativa, nao atribuir o relato a codec, internet,
  TorBox, memoria ou EOF com certeza.

## O que mudou no site e nao migra automaticamente ao NRO

Os commits web `29a6b6f7` e `c8180952` corrigiram, respectivamente, fim com
stall/foco na lista de episodios e o botao de abertura roubando foco da TV.
Mudam JavaScript/CSS, nao o player SDL/FFmpeg nativo. Nao copiar o tratamento
de EOF do navegador cegamente: no NRO, uma falha de rede nao pode ser marcada
como fim assistido apenas por estar perto dos creditos.

A API tambem evoluiu: preferencia administrativa de fonte de anime e HLS
obrigatorio em clientes iPhone/TV. O contrato de `/segments` nao mudou no diff
auditado. Os novos campos de variante sao aditivos; nao foi demonstrada uma
quebra de contrato que explique este incidente no NRO.

## Passagem de episodio: resultado e lacuna

O caminho nativo e `player_run` -> resultado natural/Proximo ->
`play_episode_sequence_run` -> `choose_next_episode` -> nova resolucao/play.
Os testes com o loop C real e seletores atuais confirmam avancar na ordem,
virar temporada e reassistir T3 sem saltar ao progresso mais recente da T5.

**Lacuna de recuperacao:** o retorno zero de `resolve_and_play_details` encerra
a sequencia. Ele representa tanto cancelamento quanto alguns caminhos de
falha de fonte/preparacao/progresso. Em particular, progresso indisponivel no
fallback de `resolve_and_play_resolved` conserva o checkpoint, mostra toast e
retorna, em vez de oferecer recuperacao contextual ali. Nem toda falha de fonte
e silenciosa: `resolve_open_with_animation` ja possui dialogo de retry explicito.

Ampliamos a simulacao do loop real: EOF natural e Proximo manual, virada de
temporada, falha antes do primeiro play e falha na abertura do seguinte. Na
injecao de falha do seguinte, a sequencia termina. Isso demonstra a consequencia
do contrato de retorno, **nao reproduz o erro HTTP/FFmpeg nem comprova a causa
do fechamento relatado no aparelho**. Nao simula crash Horizon/NVTEGRA.

Correcao recomendada para uma implementacao posterior: distinguir cancelamento,
erro recuperavel e encerramento natural; conservar o episodio de destino e
oferecer Tentar novamente/Voltar quando a tentativa falhar. Repetir POST de
criacao de sessao exige confirmacao ou idempotencia comprovada; nunca retry cego,
salto por cima do episodio, sobrescrita do checkpoint ou EOF artificial. Acrescentar
trace de handoff com IDs/resultado, sem URL/sessao textual/token.

Na virada de `season_group`, o detalhe da serie-irma e carregado de forma
assincrona: esse caminho pode sair temporariamente do player por desenho.
As simulacoes atuais nao substituem um teste integrado dessa troca com rede real.

## Botao Pular abertura

Geometria auditada em 1280x720: retangulo x450/y508, 380x68, centro horizontal640.
Termina em y576, deixando24px ate a timeline y600. A legenda reserva12px acima;
quatro linhas de38px continuam abaixo do cabecalho. O cartao Proximo esta em
x872/y432 com360x118, separado horizontalmente do botao (termina em x830).

Desenho e touch usam as mesmas constantes. O botao nao seleciona sozinho ao
aparecer; exige Direita/A ou soltar o mesmo dedo dentro da area. Menu de faixas,
episodios, scrubbing e buffering nao devem desenha-lo. Acrescentamos verificacoes
dos estados ocultos, margens e separacao dos controles. Nenhuma mudanca de posicao
foi necessaria nesta auditoria. Isto verifica geometria/logica; nao e screenshot,
medicao de fontes reais, GPU, overscan de TV ou validacao de precisao da marcacao
de um episodio real.

## Ferramenta host

O build Linux do harness real nao incluia `intro_skip.c`, apesar de `player.c`
agora chamar suas funcoes. Corrigida a lista de fontes em
`tools/host_player/build.sh`; `bash -n` passou. WSL nao esta instalado neste PC:
**nao executamos nem afirmamos aprovado o build/suite Linux real**. Resultados
antigos daquele harness nao homologam automaticamente a .60.

## Testes desta rodada

- `tools/test_episode_sequence.mjs`: loop C real; resolucao/player e selector
  de destino simulados. Acrescentados EOF/manual, virada e falhas de fronteira.
- `tools/test_episode_rewatch.mjs`: oito cenarios com seletores/loop C reais.
- `tools/test_intro_skip.mjs`: 212 ciclos pthread/join, API/render simulados,
  controles extraidos e geometria; estados ocultos adicionais.
- `tools/test_player_flow_guards.mjs`: EOF/recurso incompleto/cancelamento,
  historico e pausa em simulacao, sem console.
- `tools/test_playback_resolution.mjs`: 1.000 transicoes com pthreads reais,
  falhas/progresso desconhecido/cancel/retry explicito e limites do callback
  HTTP. Infraestrutura de rede/SDL simulada, sem conta real.
- `tools/test_player_supervisor.mjs`: supervisor `player_run` real com
  pipeline simulada, pausa/seek/retomada/negativas e conservacao do checkpoint.
- Consultas do remoto/health, `bash -n` do build host e `git diff --check`.

Todos os comandos listados passaram nesta rodada. NRO preservado:24.311.631bytes,
SHA256 `0c1a5a6208109ec97e21abc13f7ea80370c25f58a6e58a1e3cf4cf3c9ff87157`.
Nao foi rodada nova bateria completa de release: nao ha novo binario candidato.

Primeira tentativa GCC foi bloqueada pelo sandbox MSYS antes da compilacao;
execucao autorizada fora do sandbox passou. Nao contar aquela tentativa como
falha do aplicativo. Sem build ARM64 novo, conta/midia de producao, hardware,
publicacao ou alteracao de preferencia do usuario nesta rodada.

Para concluir a causa do incidente: repetir Proximo automatico, manual e painel
Episodios na mesma obra; preservar imediatamente `nplay-player-trace.log` e
`nplay-network-trace.log`. Distinguir voltar ao catalogo de o sistema fechar o
software; neste ultimo caso tambem obter o crash report do Atmosphere.
