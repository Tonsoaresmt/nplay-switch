# Auditoria de sistema/player .62 — 24/11/2026

## Resultado e limites

Esta rodada identifica **sete lacunas locais reproduzidas e um risco
condicional de protocolo**, além de melhorias de verificação. Não altera o
aplicativo, não recompila NRO, não implementa correções, não faz commit/push,
deploy ou release. O pedido foi auditoria aprofundada.

Base do NRO: v0.12.62, commit
`563245ff1a96a3b412d116b0bc7347b530d2bc48`; HEAD documental `989a71d`.
Checkout `switch-access-expiry`, branch `codex/switch-player-hardening`.
Servidor consultado SOMENTE por código: `origin/main`
`1e59abdf37648141b52a6fe6c1adf9574731da64`, referência atualizada por fetch.
Nenhuma consulta autenticada a conteúdo/conta/produção foi feita.

NRO local preservado, 24.315.727 B/SHA-256
`2899776a1ec2e1a620a0b971b4890a9312fcc06a2ab3dd0a089a25914ba48d31`.
Fontes/include/Makefile permanecem iguais à base publicada. As ferramentas
novas são negativas: **exit 0 significa defeitos reproduzidos, não aprovação**.

| Prioridade | Área | Evidência | Efeito possível |
| --- | --- | --- | --- |
| P1 | Reautenticação no mesmo processo | C real de limpeza, seleção de perfil e aplicação de respostas | Histórico/preferências antigos aceitos na identidade nova |
| P1 | Legenda PT recebida em etapas | Dois resultados do bloco C real | Fica na faixa forçada mesmo após chegar PT completo |
| P1/P2 conforme payload | Orçamento de JSON | Writer/cJSON reais, alocações contadas | Resposta de 24 MiB usa cerca de 56 MiB antes da liberação |
| P2 | HLS e audiodescrição | Master real de teste + parser/mapeamento/política C | Escolha automática PT pode ser audiodescrição |
| P2 | Áudio após o último vídeo | fMP4 real + controle PCM/escalonador C | Limite PCM provoca recuperação indevida |
| P2 | Vídeo VFR com intervalos longos | MP4 real, PTS 0/4/8 + espera/clock C | Quadro apresentado cedo, favorecendo dessincronização |
| P2 | URL longa no descritor | API C real, HTTP fake | URL assinada truncada aceita como sucesso |
| P2, condicional | Stop atrasado do mesmo aparelho | HTTP abort real + SQLite + SQL atual do backend | Encerramento antigo pode atingir sessão nova do mesmo item |

P1 significa prioridade de correção, não comprovação de um incidente específico
no console. Não houve prova de qual dessas causas atingiu VisionQuest/TBBT.

## 1. Identidade: 401 e novo login não são iguais à troca normal de perfil

`session_invalid_to_login` limpa token/usuário/perfil e retorna à entrada, mas
não invalida toda a geração de jobs, caches de histórico e respostas de
configurações em andamento. Após novo QR/senha, o caminho
`g_profile_required` de `input_profiles` ativa o perfil no mesmo processo.

O cenário executa essas funções reais e depois `pump_history` /
`pump_settings_status`, com respostas que pertenciam ao contexto anterior:

- histórico antigo continua em memória e a resposta antiga é aplicada;
- resposta antiga de Assistir mais tarde é despachada para o store do perfil
  novo; o teste registra o despacho, não grava SD nem prova exclusão física;
- `audioPref: leg` antigo substitui Dublado da identidade nova, pois a revisão
  local de preferências não identifica conta/perfil/geração.
- a flag de preferências já carregadas também sobrevive. O gate C real da
  landing não inicia consulta automática das preferências da conta nova.

HTTP, SD e store são simulados. Isso demonstra isolamento lógico insuficiente
no cliente, **não quebra de autorização da API nem acesso remoto a outra conta**.
Uma reprodução também pode ocorrer com respostas já prontas, ainda não aplicadas.

Contraprova saudável: a troca NORMAL de perfil chama restart e mantém o
contexto antigo até encerrar o processo. Não foi classificada como vulnerável.

Correção sólida: geração de autenticação + conta/perfil imutáveis por job,
cancelamento e descarte de respostas de geração antiga; limpar caches privados
na troca de identidade confirmada. O perfil HTTP não deve ser obtido de um
global mutável depois que o job foi criado. Preservar o comportamento atual de
NÃO deslogar por erro de rede/TLS. Testar também conta A→B→A e perfil removido.

## 2. PT forçado primeiro, PT completo depois

Probe 1 entrega apenas PT forced. A .62 agenda a faixa automaticamente,
esvazia `subtitle_auto_language` e captura uma escolha válida. No cenário,
essa faixa fica aplicada. Probe 2 entrega o PT de diálogo completo:
o catálogo cresce e o seletor por idioma escolheria a faixa completa, mas a
intenção automática já foi apagada; a seleção continua na forçada.

Não é o mesmo teste da .62 com ambas as faixas presentes desde o início,
que continua passando. Também não é motivo para substituir escolha forçada
deliberada do usuário.

Correção sólida: separar origem AUTO/MANUAL da escolha aplicada e conservar
idioma desejado/estado de descoberta. AUTO pode promover fallback para faixa
melhor quando chega; MANUAL/OFF não podem ser alterados. Exigir esses casos
em ambas as ordens, incluindo falha ao carregar a faixa nova.

## 3. O writer tem teto, mas várias chamadas JSON não o fornecem

`net.c:write_cb` implementa o limite corretamente. Porém `CatalogFetch`, GET
geral da API, landing, histórico, jobs e configurações usam membuf inicial
com `limit=0`. A proteção de covers/descritor/legendas não limita esses caminhos.

Fixture de JSON válido recebida em chunks, com writer e cJSON reais:

- corpo: **25.165.838 bytes**;
- buffer HTTP reservado: **33.554.432 bytes**;
- pico de alocações contado incluindo parse: **58.720.395 bytes**;
- headers do alocador, TLS, demais threads e GPU não estão nessa conta.

O mesmo callback com limite explícito 3 MiB interrompe corretamente em 3 MiB.
Não foi provocado OOM nem demonstrado esse payload no servidor real. É falta
de orçamento por chamada, não overflow de C ou prova de ataque/crash atual.

Correção sólida: medir tamanhos legítimos por endpoint; tetos por rota, contagem
de objetos/episódios, paginação e orçamento conjunto de jobs. Não impor cegamente
o mesmo teto pequeno em catálogo completo e descritor. Testar Content-Length
ausente/mentiroso, chunked, HTML/erro grande e OOM em cada etapa, preservando o
último cache válido sem interpretar resposta parcial como catálogo vazio.

## 4. O conserto de dispositions nativas não cobre HLS

O master de teste publica duas renditions PT, com nomes técnicos. A primeira
declara `CHARACTERISTICS="public.accessibility.describes-video"`. O parser e
`MasterAudioTrack` não transportam essa informação até `AudioTrackInfo`.
O mapeamento e a política reais escolhem a primeira faixa; fornecer a flag
que se perdeu faz a política escolher o diálogo comum.

Os testes nativos COMMENT/VISUAL_IMPAIRED da .62 continuam passando. Precisamos
preservar papel/acessibilidade também no HLS, inclusive depois do filtro que
mantém só o áudio selecionado. Não excluir audiodescrição do menu: apenas
não escolhê-la involuntariamente. Testar metadado correto, ausente e ambíguo.

## 5. Áudio restante no EOF exige backpressure e relógio de apresentação

Fixture real fMP4: vídeo de 2 s, áudio de 8 s, 424 pacotes, dentro de
512 slots/4 MiB. Após o último pacote de vídeo há 281 pacotes AAC de cauda.
O teste usa esses timestamps no escalonador e aceitação PCM reais.

Mesmo simulando consumo de áudio de 1 ms por pacote decodificado, o limite de
quatro segundos é atingido no bloco **197 de 281**, com cerca de 3,990 s na fila.
O player retorna erro recuperável com áudio válido pendente. Isso não foi
medido no hardware: velocidade do decoder e dispositivo são modeladas.

A .62 corrigiu o caso de vídeo AINDA na fila; a cauda SEM vídeo continua
voltando ao FIFO sem backpressure suficiente. Não aumentar o cap para esconder.
Outra verificação necessária na correção: `cur_pos` progride normalmente nos
quadros de vídeo, não na espera final de áudio. Após drenar a cauda, o EOF deve
usar posição realmente apresentada/audível, sem retirar o guard de truncamento.
Este último ponto é inspeção de fluxo, não execução completa de áudio real.

Correção sólida: distinguir esperar espaço PCM de descontinuidade irrecuperável,
manter controles responsivos e só consumir quando houver orçamento. Cobrir
áudio mais longo/mais curto, sem áudio, EOF com erro, pause, cancelamento e seek.

## 6. O limite de 350 ms não é um deadline correto de apresentação VFR

O MP4 real de teste tem três quadros H.264 com PTS **0, 4, 8** e áudio contínuo.
Com o clock e a espera C reais, sem input/Home e começando no PTS 0, o quadro
de PTS 4 espera apenas 350 ms: `delay` é truncado e o quadro segue para upload.
O resultado modelado é apresentação **3,650 s adiantada**.

Não prova desenho/GPU reais nem dessincronização em todo anime: arquivos CFR
com quadros frequentes normalmente não atingem esse caso. A matriz anterior
de resoluções/fps constantes não cobria intervalos VFR longos.

Correção sólida: manter quadro pendente até seu prazo de PTS, devolvendo o
controle ao loop a cada fatia curta. Não usar o teto de uma espera individual
como autorização para apresentar cedo. Testar B-frames, PTS ausente, saltos de
timestamp, VFR, áudio atrasado e Home/seek enquanto há quadro pendente.

## 7. URL longa é cortada silenciosamente pelo resolver

`absolute_play_url` usa snprintf e não verifica truncamento. A API real recebeu
um play_url sintético de **2.299 caracteres**, dentro do teto de 64 KiB do
descritor, e devolveu sucesso com **1.535 caracteres**. A URL assinada deixou
de ser a mesma. Nenhuma URL de cliente foi usada ou registrada.

Isso não prova que o TorBox esteja devolvendo URLs tão longas hoje. É um
contrato incompleto: o player pode tentar uma URL inválida e culpar rede/fonte.

Correção sólida: rejeição explícita e transacional de URL/descriptor que não
cabe, com erro próprio; aumentar capacidade só se justificado e bounded. Testar
absoluta/relativa, comprimento exato, query, UTF-8, newline e fonte renovada sem
alterar a fonte anterior em caso de falha.

## 8. Stop antigo não identifica a sessão exata — risco condicional

O callback do NRO recebe session_id, mas o ignora. Envia POST por item com `{}`.
O backend atual fecha por usuário+item+device_id. Isso protege OUTRO aparelho,
mas não diferencia duas reproduções sucessivas do mesmo item no mesmo aparelho.

Teste local com HTTP real e SQLite em memória: o servidor aceita o stop antigo,
o cliente cancela a espera, uma sessão nova é criada, o handler antigo atrasado
executa o SQL real do backend. Sessões velha E nova ficam inativas; outro
aparelho permanece ativo. Atraso/ordem foram controlados artificialmente;
Fastify/auth/produção não foram executados. **Não há prova da frequência nem
de que a fila de produção tenha produzido essa ordem em um incidente real.**

Contraprova de projeto: o mesmo teste com condição adicional de session_id
encerra só a sessão antiga. É SQL hipotético, não uma implementação/deploy.

Correção sólida coordenada cliente/servidor: encerrar sessão exata com ownership
de usuário/aparelho verificado; operação idempotente; não ignorar request antiga
e depois fechar tudo por item. Estudar o mesmo fencing para progresso atrasado,
sem presumir que abortar HTTP desfaz uma mutação já aceita pelo servidor.

## O que foi conferido como protegido

Regressões positivas reexecutadas e aprovadas: publicação/cancelamento tardio
(6.000 cenários), descoberta pthread, guards de histórico/EOF, recuperação de
login (2.200 requests/join), demux pthread e modo positivo das correções .62.
Este último inclui 8.800 layouts/50.248 chamadas de política, 200.000 cues e
a fixture real de áudio ANTES do vídeo que motivou a .62.

Esses resultados não contradizem as lacunas: testam fronteiras diferentes.
Não foi reexecutado build/validador de release completo porque não há candidato
novo. O NRO publicado ficou intocado.

Além dos oito pontos, permanecem pendências conhecidas, sem afirmar nova causa:
startup/troca manual de legenda hot ainda têm modal; decoder de imagem pode
ter pico antes do resize; muitos timers visuais usam comparação absoluta de
ticks; harness Linux ainda não rodado neste ambiente. Não foram promovidos a
defeitos físicos comprovados nesta auditoria.

## Plano de melhoria e critérios de aceite

1. **Identidade/contratos:** geração por conta/perfil/attempt, descarte de
   resultado antigo, URL estrita e orçamento JSON por rota. Aceite: A→B→A com
   respostas em qualquer ordem; nenhum cache/pref/arquivo de B recebe dados de A.
2. **Legendas/áudio:** papel HLS, origem AUTO/MANUAL/OFF e upgrades tardios
   transacionais. Aceite: forced→full e full→forced, erro/cancel entre respostas,
   nenhum override manual e nenhum restart de vídeo para baixar só texto.
3. **Tempo de mídia:** backpressure de cauda PCM e quadro pendente com deadline.
   Aceite: fixtures VFR/cauda rodam até o fim sem reconexão falsa, sem áudio
   descartado, sem aumentar cap e com input responsivo durante a espera.
4. **Sessão fim a fim:** stop/progresso com fencing, teste HTTP/SQLite e servidor
   real isolado antes de deploy. Aceite: request atrasada nunca encerra ou
   sobrescreve uma reprodução posterior, nem outra tela da mesma conta.
5. **Prova integrada:** CI/harness executando player completo com FFmpeg 7.1 e
   SDL de teste, além do CLI 8.1.1. Depois medir em Switch: 60+ min, maratona,
   Home/sono, rede ruim, fontes R2/TorBox/anime e heap/GPU/SSL. Testes host não
   autorizam anunciar homologação do aparelho inteiro.

Cada reprodução negativa deve virar teste POSITIVO da correção correspondente.
Não publicar várias mudanças concorrentes sem medir seu efeito separadamente.

## Reprodução

```powershell
# HOST_CC = GCC MSYS2; Node, FFmpeg e ffprobe no PATH
node tools/audit_player_062_edges.mjs --reproduce-062 --media-fixtures
node tools/audit_stop_identity_062.mjs --reproduce-062
```

Ambos exigem opt-in NEGATIVO. O primeiro recusa dependências de runtime/helpers
que não correspondam à base .62. Não integrar esses modos ao gate verde de
release. Geração/ffprobe usam FFmpeg **8.1.1**, SDK do NRO **7.1**; não confundir.
Resultados gerados ficam em build e não entram no aplicativo.

As tentativas iniciais dos testes tiveram ajustes de include/link host e
escaping do filtro FFmpeg. Somente a execução completa posterior, com todos
os asserts satisfeitos, conta como reprodução concluída.

## Referências primárias

- [SDL_QueueAudio](https://wiki.libsdl.org/SDL2/SDL_QueueAudio) e [SDL_GetQueuedAudioSize](https://wiki.libsdl.org/SDL2/SDL_GetQueuedAudioSize): fila de aplicação não é a mesma coisa que áudio já apresentado no hardware; manter limite e backpressure no cliente.
- [FFmpeg 7.1: decoding](https://ffmpeg.org/doxygen/7.1/group__lavc__decoding.html): estados de envio/recebimento e drain. Não foi atribuído um travamento a EAGAIN sem reprodução.
- [RFC 8216: EXT-X-MEDIA](https://www.rfc-editor.org/rfc/rfc8216): papel, grupo, forced e características de acessibilidade das renditions.
