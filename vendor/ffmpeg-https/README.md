# FFmpeg 7.1 com HTTPS para Nintendo Switch

O pacote oficial `switch-ffmpeg 7.1-5` habilita `http` e `tls`, mas omite o
protocolo `https` da lista de protocolos registrados. Isso impede o demuxer HLS
de abrir os manifestos HTTPS usados pelo Nplay e produz `Protocol not found`.

`lib/libavformat.a` e recompilada a partir do FFmpeg 7.1 com os dois patches
oficiais do devkitPro. As diferencas locais sao adicionar `https` a
`--enable-protocol` e exigir a verificacao do hostname no backend TLS libnx,
alem da CA e da data do certificado. As outras bibliotecas continuam sendo as
do pacote oficial da mesma versao.

Para reproduzir o artefato, execute no ambiente MSYS2 do devkitPro:

```sh
bash tools/build_ffmpeg_https.sh
```

O script valida os SHA-256 publicados na receita oficial antes de compilar e
confirma que o simbolo `ff_https_protocol` existe no arquivo final.

Fontes:

- https://github.com/devkitPro/pacman-packages/tree/master/switch/ffmpeg
- https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz
