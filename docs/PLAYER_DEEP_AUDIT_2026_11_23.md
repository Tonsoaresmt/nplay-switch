# Auditoria aprofundada do player — 23/11/2026

Continuação: as reproduções específicas motivaram a implementação .62,
descrita em [PLAYER_FIXES_0_12_62.md](PLAYER_FIXES_0_12_62.md). Este documento
preserva o estado da auditoria anterior à correção, não o resultado atual.

## Conclusão e escopo

A .61 **não pode ser apresentada como homologação de todo o player**. Sua
correção de entrada/continuidade não cobre todos os problemas de demux,
seleção de áudio e descoberta de legendas. Esta revisão encontrou oito
comportamentos indesejados reproduzíveis no código publicado.

Esta rodada não modificou o aplicativo, não recompilou o NRO, não alterou
servidor/contas/pacotes R2 e não publicou versão. Foram adicionados apenas
ferramentas de investigação e este relatório. Não há Switch físico disponível.
Nenhum resultado abaixo prova, isoladamente, a causa de um incidente antigo
sem o trace correspondente.

Base: tag v0.12.61 / build `e684b2825cb4faf12dbf2a3790ede10d6debb7d3`,
checkout `switch-access-expiry`, branch `codex/switch-player-hardening`.
O diff .60→.61 não altera `player.c`, `audio_policy.c`, `subtitle_store.c`,
`subtitle_catalog.c` ou `hot_subtitles.c`: os achados nesses módulos são
preexistentes, não regressões atribuíveis à .61. Isso não prova ausência de
regressões em outros caminhos da .61.

NRO preservado: 24.311.631 bytes; SHA-256
`a3df1de296145f9f9b4e1f45382ab92cbead22ba0aebcdd8adc6683aacb292c9`.

## Matriz de caminhos, não uma única URL

| Caminho | O que foi investigado | Evidência e limite |
| --- | --- | --- |
| TorBox/progressivo MP4 | Range, falhas HTTP, metadados de áudio | Fixtures HTTP/FFmpeg reais passam; mapeamento C reproduz perda de disposition. Não é reprodução autenticada de VisionQuest. |
| Remux fMP4 sequencial | Ordem de pacotes, EOF com fila não vazia, preroll e limite PCM | Mídia real lida por pipe + replay do controle C reproduzem recuperação indevida. Decoder/SDL/render simulados. |
| R2/HLS de filmes/séries | Master, renditions, seek, seleção PT | Fixture real HLS/VTT e testes de seek passam; seleção C reproduz preferência indevida por PT forced. Não houve acesso autenticado a um pacote R2 de cliente. |
| Anime com legenda externa/progressiva | Descoberta tardia, idioma ausente, retry, teto, cue longo/UTF-8/letreiro | Oito achados incluem descoberta/seleção; armazenamento e regressões de conclusão passaram. Não é homologação de todos os fansubs. |
| Entrada/continuidade .61 | Checkpoint, retry, cancelamento, maratona | Testes específicos existentes passaram novamente; rede e player final são simulados. Ver resultado final abaixo. |

## Falhas reproduzidas

### 1. Remux: EOF do produtor desliga prioridade de vídeo antes de esvaziar a fila — alta

Em `player.c`, a escolha `video_first` depende de
`!demux_worker_terminal(&demux)`. O produtor pode já ter encontrado EOF enquanto
há centenas de pacotes válidos na fila. EOF do produtor não significa fila vazia.
Nesse estado, o consumidor volta à ordem FIFO, inclusive no preroll pausado.
Se vierem mais de quatro segundos de áudio antes do primeiro vídeo, o limite
PCM é atingido e produz erro recuperável, apesar de existir vídeo na fila.

Não é necessário supor um arquivo inválido: a ferramenta gera um MP4 fragmentado
com AAC 48 kHz e H.264, fragmento de 6,4 s. O ffprobe encontra:

- 455 pacotes / 590.459 bytes comprimidos, abaixo de 512 slots e 4 MiB;
- arquivo seekable: primeiro vídeo no índice 47;
- o mesmo arquivo por stdin não seekable: primeiro vídeo no índice 300,
  após cerca de 6,400 s de áudio.

A ordem real de pacotes, reproduzida no controle de escalonamento e aceitação
PCM extraídos do player publicado, atinge o limite após 188 blocos AAC-sized,
com aproximadamente 3,99 s enfileirados e vídeo ainda pendente. SDL não consome
áudio durante esse preroll simulado. O resultado é -5/recuperação, não OOM.

A fixture é curta e permite ao produtor alcançar EOF dentro do orçamento. Não
generalizar a frequência para filmes longos ou atribuir todas as reconexões a
este caso. A geração/demux usa FFmpeg **8.1.1**; o NRO usa **7.1**. O código
oficial 7.1 também distingue leitura MOV seekable e não seekable, mas falta
executar a pipeline completa com esse SDK e NVTEGRA no aparelho.

Direção de correção: separar produtor terminal de fila drenada e continuar
retirando vídeo já disponível. Cobrir também cauda só de áudio, erro terminal,
fila sem vídeo e cancelamento, sem busy-loop, descarte silencioso de PCM,
aumento cego de memória ou EOF artificial.

### 2. Áudio nativo perde a marca de comentário — alta

O mapeamento de AVStream para AudioTrackInfo em `player.c` copia idioma,
título e default, mas não `AV_DISPOSITION_COMMENT` para `is_commentary`.
Com comentário PT primeiro e diálogo PT depois, sem título revelador, Dublado
escolhe comentário. Quando a mesma flag é fornecida à política, ela escolhe
o diálogo corretamente. O defeito está demonstrado na integração dos metadados.

### 3. Remapeamento de continuidade PT ignora comentário — média/alta

`continuity_choice` escolhe a primeira correspondência de idioma sem excluir
comentário. Mesmo fornecendo corretamente `is_commentary`, a continuidade de
prioridade 1 pode escolher comentário PT antes do diálogo PT.

Não confundir com escolha manual exata de prioridade 2: esta deve continuar
respeitada, inclusive se o usuário realmente escolher comentário. O supervisor
já reduz a prioridade quando muda a identidade de fonte; esse guard existe.

### 4. Preferência Original/Legendado pode privilegiar japonês em filme — média

A política procura japonês ou título contendo Original antes de considerar
default. Um filme com inglês original/default e alternativa japonesa, sem
título textual Original, escolhe japonês. A integração não conserva a flag
`AV_DISPOSITION_ORIGINAL`. Não remover indiscriminadamente a preferência de
anime: transportar metadados/contexto e preservar seleção manual.

### 5. Automático PT escolhe forced/placas antes de diálogo completo — alta

Um master HLS real de teste contém PT `FORCED=YES` antes de PT completo
`FORCED=NO`. O parser conserva a distinção, mas o bloco automático em `player.c`
seleciona a primeira faixa de idioma PT, ignorando `forced`. Inverter a ordem
altera o resultado. Ter algum texto PT não garante ter as falas traduzidas.
Escolha automática deve preferir diálogo completo quando apropriado, sem
substituir uma escolha forced deliberada.

### 6. PT descoberto depois entra no menu, mas não ativa — alta

O bloco real que aplica `subtitle_discovery_poll` acrescenta a faixa e avisa
que ela pode ser escolhida com X; não reaplica a intenção automática quando
`scur == -1`. A reprodução com áudio japonês pode continuar sem legenda mesmo
depois de o PT chegar. A correção deve diferenciar ausência inicial de OFF
manual e não substituir uma faixa já escolhida pelo usuário.

### 7. Probe só em idioma estrangeiro encerra novas tentativas — alta condicional

`subtitle_discovery_retryable(200, 1)` é falso: uma faixa inglesa basta para
encerrar retry, mesmo se o PT puder aparecer numa sonda posterior. A simulação
comprova essa decisão, não que todo servidor acrescente faixas tardiamente.
Precisamos de critério limitado de completude/idioma desejado, não polling
infinito ou reload de vídeo para consultar metadados.

### 8. PT na 17ª posição não entra no catálogo — média condicional

`hot_subtitle_tracks` respeita a capacidade 16 e interrompe a leitura. A fixture
com 16 EN antes do PT reproduz sua ausência. É um limite seguro de memória,
mas sem garantia de prioridade linguística. Não explica automaticamente um
caso com cinco faixas. Não aumentar sem limite nem trocar índices publicados
durante o uso; selecionar/paginar por identidade estável antes da exposição.

## O que resistiu aos testes e o que não foi provado

- 8.800 layouts de faixas / 50.248 chamadas reais de política: Dublado comum
  seleciona PT não comentado quando os metadados são corretos; escolha manual
  exata é preservada. Isso não anula os defeitos de integração/continuidade.
- 200.000 cues reais no store: consulta até o último cue, 2.088.890 bytes de
  texto, inserção além do teto rejeitada e cues antigos continuam legíveis.
- Inspeção do caminho progressivo: texto é copiado sob mutex antes de liberar;
  letreiros são estruturas com texto próprio, não ponteiros para memória que
  o worker pode realocar. Não foi encontrada nesse trecho a suspeita de UAF.
- GCC `-fanalyzer` não emitiu diagnóstico nos cinco módulos isolados
  audio_policy, subtitle_store, subtitle_catalog, hot_subtitles e subtitle_choice.
  Não equivale a provar ausência de corridas/OOM no aplicativo inteiro.
- Tentativa de UBSan não executou: o compilador host não possui `-lubsan`.
  Não contabilizar como teste aprovado. WSL não está instalado; o harness
  integrado Linux/SDL/FFmpeg não foi executado neste ambiente.
- Testes de conclusão de legenda (19 casos), descoberta/cancelamento pthread,
  uso/UTF-8/letreiros/retry, AV resume (400 epochs/40.000 blocos), controles
  (200.000 passos), HLS/VTT real e entrega HTTP/FFmpeg real passaram novamente.
- Resolução/entrada da .61 passaram novamente: 1.000 transições com ownership
  e joins conferidos, 1.000 ciclos do guard de entrada, checkpoint desconhecido
  não vira zero, POST ambíguo não é repetido automaticamente, cancelamento e
  auth continuam distintos. API/SDL/player simulados; threads pthread reais.
- O teste de uso registra um limite ainda existente: um EOF HTTP 200 precoce
  sem erro nem marcador de completude pode parecer uma legenda completa.
  Não há evidência de que esse seja o incidente específico do usuário.

`git diff --check` passou. O diff local de `source`, `include` e `Makefile`
está vazio; tamanho/hash do NRO foram reconferidos iguais à .61 publicada.
Não foi rodada novamente a bateria completa de release nem um novo build:
este é um conjunto ampliado de investigação, não um novo candidato validado.

Os testes reais de mídia não executam a interface SDL/NVTEGRA do Switch.
Faltam sessão 60+ min, Home/sono/rede reais, troca de episódio com libnx,
faixas do conteúdo de produção e medição de heap/SSL/GPU. Não prometer que
qualquer resolução/codec/TorBox passa apenas porque os testes locais passam.

## Como reproduzir esta auditoria

PowerShell, a partir do checkout ativo, com HOST_CC apontando para o GCC MSYS2
e Node/FFmpeg/ffprobe no PATH:

```powershell
node tools/audit_player_paths.mjs --reproduce-061 --media-fixture
node tools/test_subtitle_completion.mjs
node tools/test_subtitle_discovery.mjs
node tools/test_subtitle_usage.mjs
node tools/test_av_resume.mjs
node tools/test_seek_controls.mjs
node tools/test_hls_player_fixture.mjs
node tools/test_player_delivery_fixture.mjs
node tools/test_playback_resolution.mjs
node tools/test_playback_entry.mjs
```

**O primeiro comando é NEGATIVO: exit 0 significa que as falhas foram
reproduzidas, não que a versão foi aprovada.** Não integrá-lo como gate verde
do validate_release. O script extrai trechos da tag publicada; nesta rodada
os módulos auxiliares locais são os mesmos da base auditada. Arquivos gerados
ficam em build; não são fontes do NRO nem conteúdo de clientes.

## Ordem recomendada da próxima implementação

1. Corrigir EOF/fila não vazia, com a fixture real acima e negativos adicionais
   para tail drain, áudio ausente, vídeo ausente, pausa/cancel e limites.
2. Preservar dispositions no áudio e remapear continuidade sem escolher
   comentário inadvertido; testes de filme original, anime e manual explícito.
3. Separar PT completo/forced, intenção automática/OFF e descoberta incompleta;
   testar chegada tardia, erro, troca de faixa/obra e fonte sem PT.
4. Converter cada reprodução negativa em regressão positiva da implementação;
   rodar depois a bateria inteira e build ARM64 final no artefato imutável.
5. Testar no Switch os caminhos TorBox/R2/anime, seek/troca de faixas e sessão
   longa antes de anunciar homologação. Publicação, se autorizada, deve dizer
   explicitamente o que ainda depende de confirmação física.

Não reduzir TLS, aumentar tetos indiscriminadamente, fabricar progresso/EOF,
trocar fonte globalmente ou repetir POST ambíguo para mascarar esses problemas.

## Referências primárias usadas na análise

- [FFmpeg 7.1: seleção de amostras MOV e comportamento não seekable](https://ffmpeg.org/doxygen/7.1/mov_8c_source.html#l10437).
- [FFmpeg 7.1: AVStream/dispositions](https://ffmpeg.org/doxygen/7.1/avformat_8h.html).
- [FFmpeg: contrato send/receive e drain](https://ffmpeg.org/doxygen/7.1/group__lavc__encdec.html).
- [SDL_QueueAudio](https://wiki.libsdl.org/SDL2/SDL_QueueAudio): a API não impõe teto próprio, mas **o Nplay já impõe quatro segundos**; não repetir o diagnóstico antigo de fila ilimitada.
- [RFC 8216: renditions HLS, FORCED e características](https://www.rfc-editor.org/rfc/rfc8216).
- [W3C WebVTT: configurações dos cues](https://www.w3.org/TR/webvtt1/#webvtt-cue-settings).
