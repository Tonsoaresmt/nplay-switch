# Auditoria de uso de legendas — 05/10/2026

## Resultado e limite da conclusao

Auditoria do **NRO nativo**, SDL2/FFmpeg/libcurl, no checkout
`C:/NplaySwitch/.codex-tmp/switch-access-expiry`, branch
`codex/switch-player-hardening`, HEAD auditado `c8ef79f`.

Foram reproduzidos defeitos adicionais de selecao, extracao e composicao.
**Nao foram corrigidos nesta rodada de analise.** Nao houve alteracao de codigo
do aplicativo, NRO, backend, conta, pacote R2 ou release. A 0.12.50 continua um
candidato local; a publicada continua sendo a 0.12.49. Recomenda-se segurar a
publicacao do candidato ate corrigir os pontos criticos abaixo.

As reproducoes executam funcoes C reais extraidas do player e os modulos reais
SubtitleStore/SubtitleQueue. FFmpeg, rede, pipeline, relogio e medida da fonte
sao simulados. Isso demonstra falhas de logica, **nao comprova qual delas atingiu
o anime do usuario**, nem substitui teste no Switch fisico ou numa fonte real.
O validador anterior passou porque nao cobria essas combinacoes.

## Reproducao local

```powershell
$env:HOST_CC='C:\devkitPro\msys2\usr\bin\gcc.exe'
& 'C:\Program Files\nodejs\node.exe' tools/audit_subtitle_usage.mjs --baseline
```

O comando gera apenas fixtures ignoradas em `build/`, compila com
`-Wall -Wextra -Werror` e executa as simulacoes. **Exit 0 significa que os
defeitos esperados foram reproduzidos, nao que o player esteja saudavel.**
Nao incluir essa auditoria no validador como regressao aprovada. Ao implementar
as correcoes, converter as assercoes para o comportamento correto.

Atualizacao de continuidade: esta auditoria agora fixa a base `c8ef79f` com
`--baseline`, pois o checkout recebeu correcoes posteriores. Os testes do codigo
corrigido ficam em `tools/test_subtitle_usage.mjs`; nao confundir os dois comandos.

Execucao final desta rodada: todas as reproducoes acima executaram como previsto.
`node tools/test_subtitle_completion.mjs` tambem passou 19/19, preservando os
guards da rodada anterior. `git diff --check` passou. Nao foi repetido build ou
validador completo: nenhum codigo do aplicativo mudou nesta auditoria.

## Prioridades e evidencias

### 1. Critico: falha de reabertura transforma faixa escolhida em desligada

Em `player_run`, `g_player_subtitle_index` e zerado antes de cada tentativa
(`source/player.c:4336`) e copiado incondicionalmente para `last_subtitle`
apos ela (`:4355`). Uma falha antes de enumerar/aplicar as faixas deixa zero.
Se uma troca anterior marcou `last_subtitle_priority`, o proximo retry trata
esse zero como **desligamento manual**, em vez de selecao ainda nao conhecida.

Reproducao com supervisor real:

1. Faixa 2 aplicada; retorno `PLAYER_RESTART_TRACK`.
2. Tentativa recebe hint 2 prioritario, mas abertura falha antes da selecao.
3. Nova tentativa recebe hint **0 prioritario**. Seletor real desliga legendas.

Correcao segura: distinguir UNSET, OFF deliberado e faixa validamente aplicada;
preservar a ultima escolha valida durante falha de abertura. OFF manual deve
continuar sendo respeitado. Testar troca/seek -> falha precoce -> renovacao,
cancelamento, fonte sem faixa e OFF -> recuperacao.

### 2. Alta: seek e mudanca de fonte perdem a identidade da legenda

`player_run` so torna a escolha prioritaria no restart de faixa, nao no seek
(`source/player.c:4328`, `:4368`). O seletor (`:2292`) ignora o hint nao
prioritario e usa a preferencia de idioma. Com duas faixas PT, selecionar a
segunda e fazer seek volta para a primeira. Podem ser completa, forcada ou SDH.

Na recuperacao com prioridade, e preservado apenas o **indice**, nao a identidade
da rendition. A simulacao troca a ordem [EN, PT] por [PT, EN]: indice 2 restaura
ingles. Nao foi comprovado que a fonte real do usuario reordene as faixas.

Correcao segura: na mesma fonte, conservar a rendition exata; em fonte nova,
remapear idioma/nome/papel/identidade validada. Nao transportar indice cego
entre manifests. Preservar OFF e informar fallback ambiguo. Testar duas PT,
forcada/completa, reordenacao, reducao da lista e troca de episodio.

### 3. Critico: um bloco grafico ignorado interrompe toda a extracao progressiva

`subtitle_store_add_at` ignora deliberadamente desenho vetorial reconhecido
como `m 0 0 l 100 100` e retorna sucesso. Mas `load_external_subtitle_data`
rejeita um store com zero cues (`source/player.c:979`).
`progressive_subtitle_block` usa esse loader para **cada bloco isolado** (`:1039`).
Um bloco valido que nao produz texto visivel devolve zero ao callback (`:1051`),
abortando a transferencia. Os retries podem reencontrar o mesmo bloco e nunca
chegar as falas posteriores.

Reproducao: bloco de fala e aceito; bloco grafico ignoravel retorna falha no
callback real, conservando apenas as cues anteriores. O decoder e simulado;
store, loader e encadeamento do callback sao reais.

Correcao segura: separar erro de parse/decode de sucesso com zero texto
suportado. Bloco isolado ignoravel deve continuar; uma faixa inteira sem texto
suportado continua indisponivel. **Nao retirar o guard de erro/EOF da 0.12.50.**
Testar grafico entre falas, bloco vazio/NOTE/estilo, fim sem texto, erro verdadeiro
e decoder com saida atrasada. FFmpeg diferencia retorno negativo de ausencia
de saida; conferir tambem o flush se o codec tiver atraso.

### 4. Alta: worker progressivo termina sem aviso durante o episodio

`progressive_subtitle_worker` tenta quatro vezes (original + 2/5/10 s), publica
`result=-1` e `done=1` quando esgota (`source/player.c:1070`). Esses campos sao
consultados na abertura (`:1136`, `:1149`), mas nao durante a reproducao apos
aplicar a store progressiva. Texto ja recebido expira normalmente; o usuario
pode ficar sem as falas seguintes sem explicacao.

Simulacao com quatro HTTP 503 confirma worker encerrado, resultado negativo
e cues antigas ainda presentes. Ausencia de monitoramento posterior foi
confirmada por leitura do loop, nao por renderer fisico.

Correcao segura: observar conclusao atomicamente no loop; indicar recuperacao
e depois falha com recarga manual. Continuar video e conservar texto antigo.
Usar cancelamento proprio e budget limitado; nao reiniciar o video, nao
desligar legenda silenciosamente nem criar retry infinito. Resultado so deve
ser lido apos publicacao de done; dados de cues exigem mutex.

### 5. Alta: fragmentos de karaoke ainda podem esconder fala real

`subtitle_store_text` reserva ate tres fragmentos pequenos antes de compor a
fala (`source/subtitle_store.c:248`). Com limite de quatro linhas, tres
fragmentos deixam apenas uma linha: a fala de duas linhas e excluida.

Teste real: `Fala importante\nsegunda linha` + `a`, `b`, `c` no mesmo intervalo
produz **`c\nb\na`**, sem a fala.

Correcao segura: priorizar fala e reservar seu espaco antes dos fragmentos;
nao apagar falas curtas legitimas como "Oi"/"E". Testar 1/2/4 linhas, grupos
pequenos, karaoke denso, placas posicionadas e ordem de insercao diferente.

### 6. Alta: replay duplica cues longas e aumenta memoria

`merge_recent` consulta apenas as ultimas 12 cues longas
(`source/subtitle_store.c:83`). A insercao fora de ordem tem dedup adicional
para cues curtas, nao para as longas. Reextracao progressiva do inicio pode
reintroduzir eventos longos antigos que ja sairam dessa janela.

Teste real: 30 eventos de 70 s, repetidos quatro vezes, crescem
**30 -> 60 -> 90 -> 120 cues** e 380 -> 1520 bytes de texto. Isso demonstra
crescimento desnecessario, nao que uma fonte real tenha atingido o teto.

Correcao segura: dedup limitado por chave completa de tempo/texto/posicao/ancoras,
com indice adequado. Nao deduplicar so por texto: placa e fala iguais em
posicoes diferentes sao eventos distintos. Testar 13+ eventos longos, replay,
extensao, fora de ordem, posicoes diferentes e teto de memoria.

### 7. Media: rajada de cues nativas pode expulsar fala ainda ativa

`SubtitleQueue` tem 32 slots. Letreiros nao expulsam falas, mas outra fala
encontrando fila cheia pode remover o primeiro item sem saber se esta ativo
(`source/subtitle_queue.c:140`). Fala 10-12 s seguida de 32 falas futuras
100+ s deixa **texto vazio aos 11 s** no teste real.

E um defeito demonstrado da politica de overflow; falta demonstrar essa rajada
no demux real. A store externa nao sofre o mesmo limite de 32.

Correcao segura: politica consciente do relogio/expiracao e preservacao de cues
ativas, sem bloquear leitura de audio/video. Avaliar unificar VOD externo onde
adequado. Nao simplesmente aumentar fila. Testar rajadas, eventos simultaneos,
ativos vs futuros, seek e limpeza.

### 8. Media: truncamento pode produzir texto UTF-8 invalido

Tres caminhos cortam por bytes, nao por caractere:

- `subtitle_queue.c:154`: 510 ASCII + `e` acentuado deixa o primeiro byte UTF-8
  isolado no buffer de 512 bytes.
- `player.c:710` (`ass_to_text`): 398 ASCII + caractere de dois bytes deixa o
  primeiro byte isolado no buffer de 400.
- `player_ui.c:209` (`wrap`): fallback de palavra longa corta em 60 bytes;
  ASCII + texto japones pode gerar linhas invalidas.

Conversor/wrapper reais foram executados; medida de texto foi simulada. Efeito
grafico pode ser caractere substituto ou rejeicao do renderer; nao afirmar
que esse defeito sozinho explica desaparecimento total. SubtitleStore externo
ja protege sua propria fronteira UTF-8.

Correcao segura: helper unico de copia limitada UTF-8 e quebra por fronteira
de caractere/palavra com medida real. Testar acentos, CJK, emoji, fim do buffer
e entrada malformada; preservar os limites de memoria.

## Outros limites e diagnosticos

- HTTP 200 seguido de EOF limpo de VTT progressivo nao carrega um marcador de
  extracao completa. Simulacao mostra que uma resposta curta valida e aceita.
  **Nao prova truncamento em producao.** Longos intervalos sem legenda podem
  ser legitimos: nao detectar falha apenas pelo tempo sem texto. Completeness
  exigiria contrato autenticado do backend, ligado a mesma tentativa/faixa.
- `external_subtitle_count` (`player.c:772`) consulta a store do wrapper, nao
  a store do worker progressivo. Um trace pode dizer `cues=0` apesar de haver
  texto. Contagem/cobertura/estado reais precisam ser lidos sob mutex, sem
  registrar URL assinada ou identificador de sessao.
- Preferencia local `off` sobrepor modo Legendado foi reproduzida, mas e politica
  de escolha lembrada, nao classificada aqui como defeito. Explicar/resetar essa
  preferencia na UI pode evitar confusao.
- Bitmap PGS/DVD, layout ASS completo e todos os recursos WebVTT de navegador
  nao estao implementados. Percentuais de posicao/linha e ancoras possuem
  tratamento proprio, mas isso nao equivale a paridade completa de estilos,
  regioes e escrita vertical. Nao prometer legenda para todo codec.
- Nao apareceu evidencia nova de certificado/expiracao como causa destes
  defeitos. TLS, tetos de download e cancelamento isolado devem ser mantidos.

## Ordem recomendada antes de uma release

1. Corrigir continuidade da escolha e o bloco ignoravel; regressoes do fluxo
   completo, conservando os 19 guards de completion da 0.12.50.
2. Monitorar falha progressiva, remapear identidade e eliminar duplicacao de replay.
3. Corrigir prioridade das falas, overflow nativo e UTF-8; revisar diagnostico.
4. Rodar validador completo e build ARM64. Testar fonte R2/HLS, remux progressivo,
   VTT direto e legenda embutida, dois idiomas e duas PT, seek para frente/tras,
   pausas, 503/404, cancelamento e troca de episodio. Validar no Switch com logs
   atuais e episodio com karaoke/placas. So entao publicar candidato para teste.

NRO permaneceu com SHA-256
`1894f12f48b003800a286d4517f4006918f53ffc0f27b402474d6abdd04d7560`.
Nao foi recompilado nesta auditoria: codigo do app permaneceu inalterado.

## Documentacao oficial consultada

- [FFmpeg 7.1 — avcodec_decode_subtitle2](https://ffmpeg.org/doxygen/7.1/avcodec_8h_source.html):
  erro negativo, ausencia de saida e flush de codecs com atraso sao estados
  distintos. Base para nao confundir bloco sem texto com falha de rede/decode.
- [W3C WebVTT](https://www.w3.org/TR/webvtt1/): UTF-8 e modelo de cues/posicionamento.
  Base para preservar fronteiras de caracteres e delimitar paridade visual.

Sem consulta a credenciais ou dados de contas em producao.
