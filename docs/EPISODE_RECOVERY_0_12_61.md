# Recuperacao e continuidade nativas — .61

## Pedido e implementacao

Em 23/11/2026, apos a auditoria, o usuario pediu implementar e esclareceu que
quer a maratona sem precisar apertar nada. Mantido autoplay normal: a contagem
regressiva termina sozinha em5s; a preferencia `autoplayNext` continua respeitada.
Nao forcar essa preferencia para todas as contas nem interpretar falha como EOF.

Mudancas de runtime apenas em `main.c`, `api.c`, headers e versao. O decoder,
pipeline FFmpeg/SDL, TLS e servidor nao foram modificados. O botao de abertura
ja estava centralizado em450/508/380/68; geometria preservada e tests ampliados.

### Recuperacao automatica segura

1. O worker paralelo de progresso repete somente GET de checkpoint, ate3 leituras
   de2s/5s por tentativa. Intervalos200/400ms em fatias cancelaveis. Resposta
   valida nula e diferente de falha; ausencia de progresso nunca vira posicao0.
   HTTP401/403/404/429, TLS/certificado e outros erros fora da allowlist nao
   recebem retry automatico. Schema invalido pode consumir o limite, sem virar
   checkpoint valido. B/cancel de resolucao para tambem a espera auxiliar.
2. O API parser distingue somente HTTP503 com `code=server_restarting`, resposta
   do guard de admissao que recusa ANTES de criar sessao. Depois do join, o
   renderer aguarda1/2/3s com animacao cancelavel e refaz o pedido, no maximo3
   vezes. Timeout, 503 generico e resposta perdida NAO qualificam.
3. Depois que esses limites e as recuperacoes existentes do player esgotam,
   o resultado interno `PLAYBACK_ENTRY_FAILED` distingue erro de cancelamento.
   A entrada conserva as copias de titulo/metadados e o mesmo item e oferece
   retry explicito. Cada nova admissao so depois de A/toque, nunca por EOF falso.
   Auth/quit/cancelamento saem sem esse dialogo. Eventos `episodes/opening-failed`
   e `opening-retry` registram somente o item, nao URLs/segredos.

O caminho comum de fim natural, Proximo manual e episodios escolhidos continua
1/2; falha interna nao escapa ao seletor como fim natural. JSON emprestado do
progresso e consumido/deletado uma unica vez em todas as tentativas.

## Cobertura e limites

- `test_playback_resolution`: funcoes C reais, API real com transporte fake e
  pthreads reais. GET falha uma/duas vezes e recupera sem input/sem outro POST,
  negacoes nao repetidas, admissao503 explicita com limites/cancel, generic503
  e timeout exigem confirmacao, progresso desconhecido conservado,1000transicoes.
- `test_playback_entry`: wrapper C extraido, resolver/player/UI fake;1000ciclos,
  episodio exato, metadados, JSON consumido/nao consumido, retry/cancel/auth/quit,
  retornos natural/manual sem dialogo desnecessario.
- `test_episode_countdown`: contagem C real com tempo/SDL fake. ZERO eventos
  leva ao proximo em5s, inclusive wrap de32bits; A imediato, B/Minus/toque/quit.
- `test_episode_sequence` e `test_episode_rewatch`: loop e seletores extraidos,
  natural/manual, virada, escolhas da lista e rewatch deT3 apesar do progressoT5.
  Stub que retorna cancelamento/falha final ainda termina o loop por desenho;
  nao confundir esse stub com a recuperacao real da entrada testada separadamente.
- `test_intro_skip`:212ciclos pthread, controles e geometria/ocultacao ampliados.
- Lista Linux agora inclui intro_skip.c; bash-n passa. WSL ausente, portanto
  nao afirmar aprovado o harness Linux integrado, pixels, GPU ou console.

Build integral `make -B -j4` passou com -Werror sem apagar build/logs. Primeira
tentativa do shell de login nao achou Makefile porque mudou o diretorio; nao
compilou nada, e o comando com cd explicito concluiu. Sem bypass de permissao.
Validador FINAL COMPLETO `validate_release.ps1 -SkipBuild` terminou exit0, SEM
SkipMediaFixtures, no artefato imutavel abaixo. Novos testes integrados,55casos
reais de midia/6perfis/118HTTPrequests e1.087blocosAAC, legendas/TLS/ELF passaram.
Fixtures CLI usam FFmpeg8.1.1 no PC; NRO conserva FFmpeg7.1/SDK. Nao sao execucao
do NRO/Horizon/GPU fisicos. Nenhuma fonte/app ou rebuild depois dessa prova.
Sidecar atualizado apenas apos a conclusao. NRO/sidecar nao entram no commit.

Artefato publicado:24311631bytes; SHA256
`a3df1de296145f9f9b4e1f45382ab92cbead22ba0aebcdd8adc6683aacb292c9`.

Release v0.12.61 latest publica em23/11/2026 17:59:04UTC (14:59:04Brasilia),
nao draft/prerelease. Tag/target `e684b2825cb4faf12dbf2a3790ede10d6debb7d3`.
NRO e sidecar76B conferidos no draft e por download integral publico SEM auth
apos promover; API latest confirma .61. Binarios nao foram commitados.
Nao houve rebuild ou mudanca de fonte/app apos a prova final.

Sem trace novo, nao atribuir o incidente relatado a esta lacuna com certeza.
Fontes inexistentes, problemas permanentes e perda ambigua de POST ainda podem
pedir intervencao. O fallback legado de preparacao acelerada conserva caminhos
preexistentes; esta rodada nao promete recuperar qualquer provedor sem fonte.
Nao ha teste de Switch/Horizon/NVTEGRA fisico nem midia/conta real nesta rodada.
