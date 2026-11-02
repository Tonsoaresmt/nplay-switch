# TorBox e pesquisa do console — auditoria de 01/11/2026

Base: `dec01ef`, NRO publicado 0.12.58. Checkout autorizado
`C:/NplaySwitch/.codex-tmp/switch-access-expiry`, branch hardening.

Pedido: investigar falhas principalmente no TorBox e avaliar os dados do
projeto Switch CloudCard enviados pelo usuario. A anotacao externa foi tratada
como relato de outro experimento, nao como instrucao para mudar a arquitetura,
executar make clean ou transferir automaticamente seus numeros ao Nplay.

## Conclusao e limites

O material trouxe pistas uteis. A comparacao revelou dois defeitos reproduziveis
no codigo auditado, mais uma divergencia de certificados e oportunidades de
medicao. **Nenhum deles foi confirmado como causa da tentativa real do usuario**:
faltam titulo/episodio, versao instalada e traces novos. Nao atribuir a culpa
ao Wi-Fi, ao TorBox ou ao codec com base apenas no sintoma.

Esta rodada adiciona diagnosticos/documentacao. Nao muda app, SDK, biblioteca
vendorizada, NRO, backend, conta de producao ou release. Nao houve teste fisico.

## 1. Dois transportes diferentes

Em `source/player.c`, `native_hls` abre master e segmentos pelo libcurl do app;
MP4 e remux sequencial entram no protocolo HTTP/TLS nativo do FFmpeg.

| Entrega | Caminho auditado | Implicacao |
| --- | --- | --- |
| R2/HLS | curl_avio + net_configure_curl_isolated | CA embutida, cancelamento e prazos do app |
| TorBox HLS | mesmo transporte customizado | playlist EVENT/posicionamento ainda diferem do VOD R2 |
| TorBox MP4 Range | HTTP/TLS FFmpeg | verificacao TLS nativa, sem callback CA do libcurl |
| TorBox remux restart | HTTP/TLS FFmpeg, sem Range | plano `/seek`, reabertura e base temporal explicitos |

Reconhecer container MP4 nao significa mesmo transporte, codec ou possibilidade
de seek. Os testes de PC usam outro backend TLS; sucesso no PC nao homologa
o caminho libnx do NRO.

## 2. Defeito reproduzido: espera TLS sem cancelamento interno

A biblioteca vendorizada usa FFmpeg7.1 + patch TLS oficial devkitPro, com a
correcao local de hostname. O patch oficial foi baixado e seu SHA conferido
com a receita fixada em `tools/build_ffmpeg_https.sh`:
`57ea7ec8ed26d13d3172d0fd589b7883d22f0c180d50b7434fcc73fb2b3ab7d7`.

O codigo `tls_open` repete `sslConnectionDoHandshake`/WOULD_BLOCK com sleep,
sem consultar `ff_check_interrupt` ou seu proprio deadline. `tls_read` chama
`sslConnectionRead` diretamente. O modo normal aberto por HTTP e bloqueante.
Definir `rw_timeout=30s` no chamador nao insere uma verificacao dentro dessas
chamadas. O controle externo pode checar antes/depois, nao durante uma IPC
bloqueada. `nm` do `tls_libnx.o` confirmou dependencias SSL e av_usleep, sem
referencia a ff_check_interrupt ou importacao de CA.

Reproducao local com os callbacks C oficiais, SDK/rede/relogio simulados:

- Seis respostas WOULD_BLOCK com custo artificial10s cada e erro terminal na
  setima:70,06s simulados, `rw_timeout=30s`, cancel solicitado, ZERO consultas
  ao callback de interrupcao. O erro terminal e injetado para encerrar o teste;
  nao e medicao de um handshake real nem promessa de espera de70s no console.
- Leitura SDK com custo artificial50s: callback retorna depois dessa espera,
  sem deadline/cancelamento proprio. O teste NAO prova o timeout interno do
  Horizon; prova a ausencia de uma garantia local no callback auditado.

Correcao proposta: manter o protocolo nativo, mas tornar handshake/read/write
nao bloqueantes e limitados por tempo monotono, consultar interrupt_callback
entre tentativas e manter yield/poll limitado. Mapear erro nativo por etapa,
sem registrar URLs/SIDs/tokens. Testar fechamento parcial e conexoes ocupadas.
**Nao desativar TLS nem substituir pelo AVIO antigo sem reproduzir seus erros.**

### Certificados: diferenca comprovada, falha da tentativa NAO comprovada

O libcurl importa a CA embutida em `net.c`. O TLS nativo nao chama esse callback
e usa o CertStore do sistema, conforme API libnx. Logo corrigir a CA do login
nao comprova a CA do video direto. Isso pode explicar diferenca por cadeia de
certificado, mas **nao foi demonstrado erro de certificado neste relato**.
Plano: comparar mesmos endpoints no aparelho e unificar politica de confianca
com verificacao de CA/hostname/data preservada. Nao gravar chave/URL assinada
em log, nao usar tls_verify=0 e nao deduzir causa por um EIO generico.

## 3. Defeito reproduzido: espera circular audio/video na .58

O novo agendador prioriza video quando ja ha350ms de PCM no preroll ou1000ms
tocando. Se nao encontra video, conserva audio comprimido; isso evita a perda
de PCM corrigida na .58, mas faltava cobrir um produtor sem espaco para chegar
ao video seguinte.

Reproducao com **worker C real, mutex/condicao/thread pthread reais e scheduler
real extraido do player**, com av_read_frame/SDL/decodificacao simulados:

1. Fonte sintetica entrega600 pacotes de audio antes do proximo video.
2. Worker ocupa512 slots; proximo pacote ainda nao pode ser lido.
3. PCM simulado400ms/preroll aciona video-first.
4. Em1000 consultas, consumidor retiraZERO pacotes, produtor fica com512
   leituras, terminal permanece0. Nenhuma ponta libera a outra.

Isso comprova uma condicao de espera circular no algoritmo; NAO prova que a
obra do usuario tem essa ordenacao. O teardown cancela/junta corretamente.

Correcao proposta: explicitar o estado "video ausente + produtor sem espaco",
usar resequenciamento limitado sem perder audio e definir saida diagnostica
quando a fonte nao cabe no budget. Nao descartar PCM para fingir progresso,
nem remover os tetos, nem simplesmente entrar em reconexao infinita. A reserva
temporaria precisa estar dentro de um budget total e ser validada em ambos
os sentidos (audio antes de video e video antes de audio), EOF, pausa e seek.

No backend historico disponivel (`build/site-c9d0f77`, NAO prova do deploy
atual), remux usa frag_keyframe e copia video salvo AVI. Avaliar tambem
frag_interleave/frag_duration em teste A/B, sem deploy automatico. Isso pode
limitar rajadas entre faixas, mas nao substitui a protecao do consumidor.

## 4. O que aproveitar da pesquisa externa

- **Sockets:** Nplay ainda chama socketInitializeDefault sem conferir retorno.
  Fonte oficial tem3 sessoes BSD, RX inicial64KiB/max256KiB. Vale teste A/B
  com configuracao explicita, verificacao de inicializacao e telemetria. Nao
  assumir ganho17% ou escolher12 sessoes sem medir concorrencia/heap real.
- **BSD versus SSL:** pools de sessoes IPC diferentes. A API SSL aceita1..4
  sessoes; isso NAO significa4 conexoes TLS ou4 SslContexts. Curl inicializa3.
  Aumentar sessoes BSD nao amplia o pool SSL. Falta medir contextos/conexoes
  efetivamente ativos antes de concluir exaustao.
- **Banda:**4,3–4,5MB/s e resultado daquele ambiente, nao teto universal do
  hardware. Medir throughput sustentado e picos do conteudo, nao so velocidade
  media. Mais conexoes podem disputar a mesma banda.
- **Concorrencia:** o pool HLS_IDLE_HANDLES=4 limita handles OCIOSOS, nao o
  numero de downloads ativos. Prefetch tem ate4 workers; video, audio, legenda
  progressiva e heartbeat podem concorrer. Instrumentar ativos/picos e dar
  prioridade a midia. Nao aplicar mutex global a transferencias inteiras:
  legenda progressiva pode ficar aberta e impedir video/heartbeat.
- **curl_multi:** alternativa a experimentar, nao garantia de compatibilidade
  ou solucao para contextos. Nao trocar o transporte estavel apenas por conselho
  de outro projeto; o comentario historico sobre multi nao e uma medicao atual.
- **Memoria:**16MiB isolados nao sao "seguros" em qualquer fase. Contar fila
  demux4/12MiB, rings4MiB por recurso, quadros NVTEGRA/CPU, texturas, legendas e
  TMEM da rede. Modificar RX max256KiB->1MiB pode aumentar TMEM em cerca3MiB
  com sb_efficiency4, alem de outras reservas. O applet deve ser medido.
- **SD:** Nplay nao baixa a obra inteira no SD; prealocacao de64MiB nao corrige
  o video em RAM. Ainda vale reduzir logs sincronicos. Nao criar cache SD por
  padrao sem validar ownership, cancelamento, espaco e integridade.
- **CPU/codec:** pin de nucleo nao resolve codec pesado. O remux historico pode
  copiar HEVC/10bit etc; todos os formatos precisam de medida NVTEGRA/CPU.
  Abrir um decoder com hw_device nao prova uso efetivo do hardware.
- **Metodo:** laboratorio opcional com relatorio seguro e injecao de falhas
  no aparelho e a melhor contribuicao. Precisa consentimento/autenticacao;
  nao adicionar controle remoto arbitrario nem envio automatico de segredos.
- **Build:** nao executar make clean neste checkout: apaga fixtures e simbolos
  historicos. Usar build completo -B quando mudar versao e conferir NACP/ELF.
- Partes de NCA, NSP, FS/mitm e instalacao de jogos nao se aplicam ao player.

## 5. Comandos e resultados desta rodada

`HOST_CC=C:/devkitPro/msys2/usr/bin/gcc.exe`:

```text
node tools/audit_torbox_paths.mjs --reproduce-058
  Dois defeitos REPRODUZIDOS. Exit0 NAO significa release aprovada.
node tools/test_av_resume.mjs
  PASS:400 epocas/40000 blocos, continuidade/piso/scheduler nos casos cobertos.
node tools/test_playback_restart.mjs
  PASS:contrato C real TorBox restart com HTTP simulado.
node tools/test_demux_worker.mjs
  PASS:worker real pthread, barreiras, tetos, limpeza e25 seeks.
node tools/test_player_delivery_fixture.mjs
  PASS:FFmpeg CLI8.1.1 + HTTP localhost, Range/remux/403/503 com quadros.
git diff --check
  PASS.
```

SDK/rede fake no audit TLS; nenhum handshake Horizon, console, fonte TorBox
real ou sessao longa. CLI8.1.1 nao e FFmpeg7.1 do NRO. As suites positivas
passarem NAO invalida os novos casos negativos: faltavam essas entradas.
Nao houve build ou bateria completa novos porque app/binario nao mudaram.

NRO preservado24.299.343B/SHA256:
`9a7b052875d6fdc5ddac96b4bdccf39b10ecc9955b13a09109fe316a320c3cab`.

## 6. Proxima rodada proposta

1. Corrigir TLS nativo cancelavel/limitado + erros seguros; comparar CA.
2. Corrigir espera circular com budget e erro definido; converter audit em
   regressoes positivas antes de publica-lo como teste obrigatorio.
3. Instrumentar codec/hardware real e ativos SSL/BSD, tempos separados de
   plano/abertura/probe/primeiro quadro/rede/decodificacao.
4. So entao A/B de RX64/256/1024KiB e3/6/12 sessoes BSD, controlando mesma
   fonte, sinal, posicao e conexoes. Sem prometer ganho por simulacao.
5. Build completo, bateria completa, digest e teste fisico R2/Range/restart:
   perda curta/longa, pausa, HOME,60+min, audio/legenda e cancelamento.

## Referencias primarias

- [libnx sockets/defaults](https://github.com/switchbrew/libnx/blob/master/nx/source/runtime/devices/socket.c)
- [libnx BSD/TMEM](https://github.com/switchbrew/libnx/blob/master/nx/source/services/bsd.c)
- [libnx SSL/API](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/ssl.h)
- [libnx SSL/session manager](https://github.com/switchbrew/libnx/blob/master/nx/source/services/ssl.c)
- [devkitPro TLS FFmpeg fixado por SHA acima](https://github.com/devkitPro/pacman-packages/blob/master/switch/ffmpeg/tls.patch)
- [devkitPro curl/libnx](https://github.com/devkitPro/pacman-packages/blob/master/switch/curl/switch-curl.patch)
- [HTTP FFmpeg7.1](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavformat/http.c)
- [Fragmentacao MP4](https://ffmpeg.org/ffmpeg-formats.html#mov_002c-mp4_002c-ismv)
- [libcurl threads/share](https://curl.se/libcurl/c/threadsafe.html)
