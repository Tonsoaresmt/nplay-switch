# Player 0.12.62 — correções demonstráveis

Base publicada .61, checkout `switch-access-expiry`, branch
`codex/switch-player-hardening`. O usuário autorizou corrigir, gerar e publicar
no GitHub para atualização do console. Sem alteração de backend/contas/R2/TLS.

## Implementação

1. **EOF com vídeo ainda na fila:** a prioridade de vídeo não depende mais de
   o produtor estar vivo. A inspeção terminal/ausência de vídeo acontece sob o
   mesmo mutex, inclusive quando um pacote chega depois do primeiro scan.
   Somente a cauda terminal sem vídeo volta ao FIFO. Mantidos os orçamentos
   existentes, erro de limite e checkpoint; nenhum PCM descartado para mascarar
   a falha. A fixture curta de fMP4 não seekable da auditoria deixa de atingir
   o teto de quatro segundos antes de retirar vídeo da reserva.
2. **Áudio nativo:** preserva COMMENT/VISUAL_IMPAIRED e ORIGINAL do AVStream.
   Dublado evita comentário; remapeamento de idioma também o evita. Seleção
   manual exata na mesma fonte continua prioritária. Original explícito vence
   o fallback japonês; sem indicação de original, o fallback anterior de anime
   continua existindo. Não é possível adivinhar com certeza a origem de toda
   dublagem quando o arquivo não publica metadados corretos.
3. **PT completo antes de forced:** seleção por idioma prefere diálogo
   completo. Forced continua disponível e pode ser escolhido manualmente.
   Escolha exata/OFF preservados pelo mecanismo de identidade existente.
4. **Descoberta de legenda:** uma resposta só em inglês não encerra as consultas
   por PT. Permanecem no máximo três tentativas automáticas em segundo plano,
   com a política de erros permanentes/TLS mantida; não há polling infinito.
5. **PT tardio:** ausência automática inicial é diferente de OFF manual.
   Quando o idioma desejado chega, agenda o carregamento sem intervenção;
   manual OFF/seleção cancela essa intenção. O worker progressivo novo não
   bloqueia a renderização esperando o primeiro cue. Tem cancel próprio,
   cancelamento solicitado se a espera inicial atingir 15 s, resultado guardado por escolha e
   ownership local à tentativa. Falha conserva a faixa aplicada e usa retry
   limitado. Uma tentativa inicial de remux que falha também agenda retry.
   Cancelamento explícito (-2) não agenda retry; o teste usa a mesma faixa
   desejada/aplicada para distinguir cancelamento de falha recuperável.
6. **Capacidade 16:** parsers HLS/hot examinam faixas posteriores e conservam
   PT completo antes de PT forced/default/estrangeiras dentro do mesmo teto.
   Isso acontece ANTES da publicação do catálogo. Não reordenam nem substituem
   índices já exibidos de um catálogo vivo. Um catálogo vivo já cheio ainda
   pode impedir acréscimo tardio: não anunciar paginação ilimitada resolvida.

Startup/troca **manual** de legenda hot ainda usam o modal preexistente. Esta
rodada torna a seleção **tardia/automática e retries** não bloqueantes; não
anunciar que todos os caminhos de hot passaram a ser assíncronos. O loader
progressivo pode fazer até as tentativas internas já existentes; o orçamento
de descoberta de metadados e retry da escolha são mecanismos distintos.

## Evidências novas

`audit_player_paths.mjs --reproduce-061 --media-fixture` extrai a .61 e suas
dependências antigas: exit 0 significa DEFEITOS REPRODUZIDOS. A comparação foi
reexecutada depois das correções. Nunca usar esse modo como aprovação.

`audit_player_paths.mjs --verify-current --media-fixture` é o modo POSITIVO,
integrado ao validate_release: exige a seleção correta nos sete cenários de
faixas e vídeo retirado antes do overflow no oitavo cenário. A mídia fMP4 e a
ordem de pacotes são reais, decoder/SDL/tempo simulados. CLI FFmpeg 8.1.1 não
equivale à pipeline SDK 7.1/NVTEGRA/Horizon.

- 8.800 layouts / 50.248 chamadas de política; 200.000 cues e consulta do último.
- Novo teste do bloco real de publicação tardia: 6.000 cenários de ready
  atrasado, OFF/nova escolha, resposta falha tardia, prazo/wrap do relógio,
  preservação da faixa antiga e nenhum join antes de done. Worker/rede fake.
- Factory C real: falha de alocação, mutex/thread ausente, URL longa, limite
  de URL com offset, query existente. Rede/SDL simulados.
- Demux C com pthread real: adicionados EOF sem vídeo e corrida com vídeo
  presente no mesmo lock, além dos testes de rajada/ring/budget já existentes.
- Teste de parser HLS com 18 faixas, PT forced antes de PT completo e
  capacidade 1/16 sem aumentar o teto.
- Harness Linux ganhou o módulo subtitle_choice ausente na lista; não foi
  executado neste ambiente sem WSL/SDL/FFmpeg de desenvolvimento Linux.
- Análise estática GCC -fanalyzer dos cinco módulos atuais de áudio, manifesto,
  hot, escolha e catálogo terminou sem diagnósticos. Não é prova de ausência
  de corridas ou corrupção na pipeline física inteira.

As primeiras rodadas detectaram ajustes necessários NOS TESTES: variáveis
antigas após refatoração, constante EOF ausente no SDK fake e links host sem
hls_manifest após a nova dependência de hot_subtitles. Foram corrigidos;
essas execuções parciais não contam como bateria completa aprovada.
O novo teste de overflow inicialmente supunha que PT completo deveria expulsar
PT forced do último slot. A inspeção mostrou o comportamento correto: ambos
ocupam slots antes estrangeiros (14/15), sem aumentar a capacidade. O teste
agora exige explicitamente as duas faixas e conserva os 14 primeiros slots.

## Validação e artefato

Build ARM64 integral FINAL `make -B -j4` com -Werror, após o guard de
cancelamento, e bateria FINAL COMPLETA `validate_release.ps1 -SkipBuild`, SEM
SkipMediaFixtures, passaram no MESMO NRO. Log local ignorado:
`build/validate-0.12.62-final2.log`. Inclui 55 casos reais de mídia/6 perfis/
118 requisições HTTP, 1.087 blocos AAC/resample, TLS e símbolos ARM64/ELF.
Uma execução completa anterior não foi usada para liberar: o guard de
cancelamento motivou recompilação e repetição integral. Não houve alteração
do aplicativo/rebuild após a prova FINAL. `git diff --check` passou.

Artefato final: 24.315.727 bytes; SHA-256
`2899776a1ec2e1a620a0b971b4890a9312fcc06a2ab3dd0a089a25914ba48d31`.
Sidecar atualizado somente após a validação. NRO e sidecar não entram em Git;
serão assets da release. Não usar `make clean`, que apagaria fixtures/logs.

Sem teste físico de Switch, rede de cliente, VisionQuest/TBBT de produção,
sessão de 60+ min, GPU, Home/sono ou todas as resoluções/codecs. A publicação
habilita confirmação no console; não é garantia de inexistência de bugs.

## Roteiro de confirmação no console

1. Conferir .62 em Configurações; abrir a mesma obra que falhava no TorBox.
2. Comparar início/Continuar, saltos curtos e longos, pausa/retomada.
3. Com Dublado, confirmar PT quando disponível; testar troca manual e retomada.
4. Anime/R2 e remux: PT completo, forced separado, OFF conservado após seek,
   descoberta tardia e falha da legenda sem reiniciar o vídeo.
5. Trocar episódio automaticamente e manualmente; testar Home/sono e sessão
   longa. Em falha, preservar os traces imediatamente após a tentativa.
