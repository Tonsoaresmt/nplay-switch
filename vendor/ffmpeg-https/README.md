# FFmpeg 7.1 com HTTPS para Nintendo Switch

O pacote oficial `switch-ffmpeg 7.1-5` habilita `http` e `tls`, mas omite o
protocolo `https` da lista de protocolos registrados. Isso impede o demuxer HLS
de abrir os manifestos HTTPS usados pelo Nplay e produz `Protocol not found`.

`lib/libavformat.a` e recompilada a partir do FFmpeg 7.1 com os dois patches
oficiais do devkitPro. As diferencas locais sao adicionar `https` a
`--enable-protocol`, exigir a verificacao do hostname e usar chamadas SSL nao
bloqueantes com prazos/cancelamento. O contexto importa a mesma CA embutida
do app pelo callback `nplay_native_tls_configure`; callback ausente ou importacao
falha impedem o handshake. CA, hostname e data continuam verificados.
As outras bibliotecas continuam sendo as
do pacote oficial da mesma versao.

Para reproduzir o artefato, execute no ambiente MSYS2 do devkitPro:

```sh
bash tools/build_ffmpeg_https.sh
```

O script valida os SHA-256 publicados na receita oficial antes de compilar e
confirma `ff_https_protocol` e `ff_nplay_tls_guard_version` no arquivo final.
Patches locais: `tools/ffmpeg-libnx-tls-hostname.patch`,
`tools/ffmpeg-libnx-tls-io.patch` e `tools/ffmpeg-libnx-tls-io.h`.
O script preserva arvores anteriores, isoladas pelo hash dos patches; nao usa
make clean nem apaga fixtures/simbolos historicos.

`node tools/test_native_tls.mjs` aplica os MESMOS patches, sem fuzz, a fixture
oficial fixada por SHA-256, e executa os callbacks C com Horizon/rede simulados.
O validador exige os simbolos de integracao no ELF. Isto nao homologa um
handshake real no console.

Fontes:

- https://github.com/devkitPro/pacman-packages/tree/master/switch/ffmpeg
- https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz
