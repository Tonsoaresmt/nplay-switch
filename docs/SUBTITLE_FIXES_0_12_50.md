# Correcoes adicionais de legendas — candidato 0.12.50

05/10/2026. Checkout `C:/NplaySwitch/.codex-tmp/switch-access-expiry`, branch
`codex/switch-player-hardening`, sobre `c8ef79f`. Este documento sucede a
[auditoria](SUBTITLE_USAGE_AUDIT_2026_10_05.md); nao e mudanca no site/Android.

## Implementacao

1. **UNSET nao e OFF.** `SubtitleChoice` so e publicado quando existe uma
   escolha de faixa ou OFF explicito/lembrado. Uma tentativa que falha antes
   de selecionar nao zera a escolha anterior. Ausencia automatica de legenda
   nao vira OFF manual: outra fonte com audio estrangeiro ainda pode usar a
   politica normal de legenda PT.
2. **Continuidade por identidade.** Idioma normalizado, nome, forced e identidade
   da rendition acompanham a escolha na mesma reproducao. Lista igual na mesma
   fonte conserva a faixa exata; lista reordenada procura a mesma identidade.
   Nome/idioma/papel unicos permitem remapear uma fonte diferente. Ambiguidade
   ou faixa ausente usa a preferencia normal, com aviso para conferir em X;
   nao restaura ingles apenas porque era a faixa numero 2. OFF e conservado.
3. **Identidade nao e cache.** O hash de selecao ignora apenas parametros de
   autenticacao allowlisted e fragmento; conserva language/format e queries
   desconhecidas. Nao e registrado. Cache/download continuam usando a URL
   completa com query: nunca reaproveitar texto por esse hash de selecao.
4. **Bloco ignorado nao mata extracao.** Um bloco progressivo com pacote
   decodificado, mas sem texto suportado, pode terminar com sucesso. Desenho
   vetorial ignorado, cue vazio e NOTE/STYLE/REGION nao impedem as falas seguintes.
   NOTE contendo `-->` tambem e ignorado. Arquivo completo sem texto continua
   rejeitado, assim como erro de decoder/rede, cancelamento, EOF incompleto e
   recurso HLS pulado. Os guards anteriores da 0.12.50 nao foram removidos.
5. **Exaustao visivel.** O loop consulta done atomico antes do resultado do
   worker progressivo; uma falha esgotada produz um aviso e trace uma unica
   vez, indicando X/selecao da faixa para recarregar. Texto anterior e video
   permanecem. As tres tentativas adicionais existentes continuam limitadas;
   nao ha renovacao infinita nem uso de demux_abort para cancelar so legenda.
6. **Fala primeiro.** O compositor reserva as linhas das falas antes dos
   fragmentos de karaoke. Falas curtas legitimas continuam quando ha espaco.
7. **Replay de eventos longos.** Longs tambem sao ordenados; busca por inicio
   e chave completa encontra eventos antigos alem da janela recente de 12.
   Reenvio/expansao de fim nao duplica texto/posicao/ancoras iguais.
8. **Fila nativa protegida.** Eventos expirados sao removidos antes de inserir;
   sob pressao, conserva as falas mais proximas e descarta o futuro mais distante,
   em vez de apagar a fala ativa mais antiga. Letreiros continuam nao expulsando
   falas. **Limite de 32 nao desapareceu**: rajada com mais eventos relevantes que
   a capacidade ainda pode perder cues futuros. Nao e paridade ilimitada com web.
9. **UTF-8 inteiro.** Copias/concatenacoes da legenda, fila nativa e conversor ASS
   preservam fronteiras de caracteres. Quebra de palavra longa no HUD mede
   caracteres inteiros, inclusive CJK. Entrada malformada nao ganhou um decoder
   Unicode novo; a garantia destas funcoes e nao cortar caracteres validos.
10. **Diagnostico e descoberta tardia.** Contagem progressiva consulta a store
    real sob mutex. Probe de remux repetido tambem atualiza as chaves de faixa,
    nao so os rotulos. Tetos de memoria/download, 16 faixas e TLS permanecem.

## Evidencia reproduzivel

```powershell
$env:HOST_CC='C:\devkitPro\msys2\usr\bin\gcc.exe'
node tools/audit_subtitle_usage.mjs --baseline
node tools/test_subtitle_usage.mjs
node tools/test_subtitle_completion.mjs
```

Auditoria usa codigo/store/queue da base `c8ef79f` por `git show` e reproduz os
defeitos antigos. Exit 0 da AUDITORIA e reproducao do defeito, nao aprovacao.
O comando test_subtitle_usage e regressao positiva do codigo atual, obrigatoria
em validate_release. Harness usa funcoes C reais, mas rede, FFmpeg, medida da
fonte e pipeline sao simulados; nao executa NVTEGRA/GPU/Wi-Fi do Switch.

Resultados comparativos:

- Falha de reopen: hint 2 -> 0 prioritario antes; agora 2 -> 2. OFF manual
  continua zero prioritario apos falha.
- Duas PT/seek, fonte reordenada, forced/completa, identidade duplicada,
  ausencia de faixa, token renovado e query de idioma diferente cobertos.
- Fala de duas linhas + tres fragmentos: antes apenas `c/b/a`; agora fala
  permanece e fragmentos usam so o espaco restante.
- 30 eventos longos reextraidos quatro vezes: antes 120 cues/1520 bytes;
  agora 30 cues/380 bytes. Tetos nao aumentaram.
- Rajada nativa conserva fala ativa; copia de acento e quebra CJK nao produzem
  UTF-8 incompleto. Contagem do wrapper progressivo retorna cues reais sob lock.
- Grafico -> fala seguinte, cue vazio, NOTE com seta, decoder sem pacote,
  erro verdadeiro, cancelamento, 503/exaustao, notificacao unica e EOF 200 limpo.
- Guards de completion anteriores: 19/19 passaram. Protegem faixa aplicada
  durante erro/cancelamento/teto e rejeitam segmento HLS pulado seguido de EOF.

## Estado de validacao/publicacao

Build ARM64 completo `make -B -j4` passou sem avisos (-Werror), seguido de builds
incrementais dos ajustes finais. `validate_release.ps1 -SkipBuild` passou no
binario final, **sem SkipMediaFixtures**, incluindo a nova regressao obrigatoria,
fixtures HLS/VTT/chunked remux e simbolos. `git diff --check` passou.
CLI de fixtures e FFmpeg 8.1.1; SDK/NRO e FFmpeg 7.1. Nao confundir essas camadas.

Binario final: **24.250.191 bytes**, SHA-256
`909f166fd66f1068b46292d45f6b804d877a7c529db3b134f5c6453724474bbb`.
Hash anterior do candidato em SUBTITLE_COMPLETION e historico, nao este build.

Nao publicar como comprovado no hardware: Switch fisico e suite integrada Linux nao foram
executados nesta sessao. Nenhum deploy, conta, reprocessamento R2 ou release
alterado. A 0.12.50 continua candidato local; atualizador publico continua .49.

## O que ainda exige teste ou contrato externo

- Episodio completo com karaoke/placas, pausa longa, seeks nos dois sentidos,
  duas PT, troca de audio/legenda, Wi-Fi interrompido e troca de episodio no
  Switch. Enviar traces atuais se texto parar de chegar.
- EOF HTTP 200 limpo antes do fim da extracao nao distingue arquivo realmente
  completo de fonte que encerrou cedo. Nao recarregar pelo simples intervalo
  sem fala; seria preciso marcador autenticado de completeness no backend.
- PGS/DVD bitmap e layout ASS/WebVTT completo de navegador continuam fora da
  implementacao. Pacote R2 sem legenda/posicao nao e reconstruido pelo cliente.
- Relato autoplay T3 -> ultimo visto T5 continua nao reproduzido; testes reais
  do controle/seletores passam, mas falta titulo/episodio/trace do incidente.

## Como continuar

Preservar a branch/checkout acima e todas as regressoes. NRO e artefato ignorado,
nao commitar. Ao publicar para teste, reconstruir, validar, comparar SHA-256 e
tamanho com o asset da Release e registrar esse fato separadamente. Nao alterar
a identidade do cache para remover query. Nao interpretar teste simulado como
confirmacao de playback fisico nem completar legenda pela ausencia de texto.
