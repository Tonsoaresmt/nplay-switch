# TorBox/player — candidata LOCAL 0.12.59

01/11/2026. Base dec01ef/.58 publica; checkout switch-access-expiry,
branch codex/switch-player-hardening. Usuario autorizou corrigir o que fosse
demonstravel sem poder fornecer logs; confirmou apenas VisionQuest.
Nao foi confirmado episodio/versao/fonte exata da tentativa.

## Correcoes implementadas

### HTTPS nativo limitado e cancelavel

O caminho MP4/remux do TorBox continua no protocolo HTTP do FFmpeg, que trata
Range e restart. Nao foi substituido pelo AVIO antigo por blocos. R2/HLS
continua em libcurl, com os cancelamentos independentes das legendas.

O patch local do FFmpeg7.1 exige SslIoMode_NonBlocking. Handshake/read/write
consultam interrupt_callback e tempo monotono entre chamadas SDK. O prazo e
rw_timeout quando positivo, ou30s por operacao. Esperas WOULD_BLOCK cedem em
10ms; chamadas com AVIO_FLAG_NONBLOCK retornam EAGAIN imediatamente. Leitura
zero continua EOF e escrita parcial e devolvida ao HTTP, nao descartada.

Falha de configurar SSL nao chega ao handshake. Erros sao registrados por
etapa/codigo, sem host, URL, query, SID ou certificado. O hook diagnostico nao
altera o renderer. A biblioteca e recompilada para ARM64 com os patches
oficiais verificados por SHA; exporta ff_nplay_tls_guard_version=59.
O validador exige guard e callbacks no ELF, nao apenas no arquivo-fonte.

### Mesma CA embutida para login, HLS e video direto

switch_ca_import centraliza o mecanismo ja usado pelo libcurl: bundle imutavel
em memoria, E1 sem NUL, E2/E3 de compatibilidade existentes. O callback curl
continua rejeitando backend inesperado antes de converter seu contexto.
O callback nativo recebe um SslContext pelo ABI libnx e usa a mesma importacao.

Callback ausente, memoria indisponivel ou importacao falha encerram a abertura
antes do handshake. CA/hostname/data permanecem verificados; nenhum arquivo de
CA ou credencial e escrito no SD. A vida util do bundle continua cobrindo todas
as threads. Isto remove divergencia de configuracao; NAO comprova que o relato
de VisionQuest tenha sido causado por certificados.

### Fila sem espera circular dentro de um budget limitado

O caso reproduzido na .58 era:512 audios comprimidos, PCM ja suficiente,
consumidor esperando video, produtor sem slot para ler o proximo video.

Agora os indices da fila ficam no heap:512 slots ativos normalmente; podem
abrir1024/2048/4096 somente quando o consumidor nao encontra video e o
produtor esta bloqueado por espaco. A ordem dos outros pacotes e mantida,
inclusive com ring circulado. Audio comprimido nao e decodificado/descartado
para abrir espaco, e o PCM continua limitado a4s.

O teto4MiB/12MiB nao aumentou. A capacidade de payload agora desconta
4096*(sizeof(DemuxItem)+sizeof(AVPacket)); a tabela tem64KiB no ABI64.
Cabecalhos e indices sao reservados nesse calculo, em vez de somados cegamente.
AVBufferRef/side_data/overhead do allocator, decoder/texturas/SSL e o unico
pacote pendente do produtor continuam fora desse subtotal: nao confundir o
budget de fila com heap total. O pacote pendente tambem tem teto individual.

No limite de slots/bytes sem video disponivel, a tentativa sai com aviso,
mantem checkpoint e nao renova a mesma fonte em ciclo. Fontes arbitrariamente
mal intercaladas nao passam a ser universalmente reproduziveis.

A decisao e reconferida sob mutex: video pode chegar depois do primeiro scan;
um flag de espera antigo apos expandir/liberar slot nao vira erro falso.
Seek/clear reinicia os slots em512; teardown junta o worker antes de liberar.
O resumo de video nao varre a fila inteira a cada audio recebido.

## Evidencias locais

- Audit negativo da .58 continua reproduzindo os dois defeitos. Exige
  --reproduce-058 e carrega o codigo historico93c4527; exit0 NAO e aprovacao.
- test_native_tls aplica os MESMOS patches, sem fuzz, ao TLS oficial fixado
  por SHA57ea7ec8...7d7. Executa callbacks reais com SDK/relogio/rede fake:
  1000 ciclos, prazos/cancelamento, read/write/handshake, EOF/escrita parcial,
  erro na configuracao, erro CA e callback CA ausente falhando fechado.
- test_tls_ca executa importacao C real/bundle real por ambos callbacks:
  1000 contextos curl +1000 nativos, E1/E2/E3, NUL/71 certificados, OOM,
  falhas e isolamento de diagnostico por thread. SSL Horizon simulado.
- test_demux_worker usa produtor e fila C reais +pthread real:25 seeks,
  barreiras,600/1800/3000 audios antes do video, ring circulado, ordem,
  clear/reset, limite4096 e limitebytes, OOM e duas corridas deterministicas.
- test_av_resume conserva400 epocas/40000 blocos; o teste de fluxo tambem
  confere ENOBUFS como erro nao recuperavel, nao EOF/completion.
- Supervisor, HOME/repouso e guard do seek HLS tambem passaram isoladamente.

O primeiro build da biblioteca exigiu corrigir um prototipo; o primeiro build
do app exigiu incluir diag.h. Uma bateria preliminar parou no harness de EOF
por falta de AVERROR/errno para o ramo novo; o harness foi atualizado com uma
assertion positiva para o erro. Nenhuma dessas falhas foi contada como teste
verde. Build completo ARM64 -Werror e incremental final passaram.

## Validacao final

Bateria FINAL `tools/validate_release.ps1 -SkipBuild` COMPLETA, SEM
SkipMediaFixtures, passou no NRO exato abaixo. Incluiu novos testes nativos,
fila pthread, controles, legendas, HOME, TLS, 55 cenarios de midia e 1087
blocos AAC com timestamps reais. Nenhuma mudanca do app/rebuild apos a prova.
`git diff --check` passou; sidecar atualizado so depois da aprovacao.

Guard e ambos callbacks nativos estao DEFINIDOS (T) no ELF, nao so como
referencias fracas. libavformat.a final SHA256:
`33f85e9b8dc8af67186ac18ba331e695cc9ee6760715c372d6d9dd3e0ddf2ecb`.
O build da dependencia teve avisos preexistentes nos headers SDK/RTMP;
o build completo do app e incremental final foram aprovados com -Werror.

NRO atual24.299.343B, SHA256:
`deed0e30de6c6576ff2f0c10f481262c1cf1016f07a511b9c2213b4223e472b9`.

## Limites e teste fisico

Nao houve Switch/NVTEGRA/Wi-Fi, handshake real Horizon, VisionQuest real,
TorBox/R2/conta de producao ou sessao60min. CLI8.1.1 nao e SDK7.1; a fixture
C nao executa o NRO. SDK nao bloqueante precisa de confirmacao no hardware.
Perfis/codec pesados, origem HLS desconhecida e lacunas legitimas grandes de
PTS continuam podendo exigir ajuste especifico.

No console, testar R2, TorBox Range e remux restart: inicio/Continuar,10/60s,
meio/voltar, pausa, audio/legenda, B durante conexao, HOME/sono, rede curta e
longa e60+min. Conferir native-tls/guard, native-tls/<etapa>, demux worker-start
(slots/payload/budget) e interleave-limit. Nenhuma URL/SID deve aparecer.

Nenhum backend/deploy/reprocessamento/conta alterado. Sem push/release; .58
continua publica. NRO/sidecar nao devem ser commitados. A biblioteca vendorizada
e dependencia de build ja versionada, nao asset NRO.

Fontes e auditoria anterior: docs/TORBOX_CONSOLE_AUDIT_2026_11_01.md.
