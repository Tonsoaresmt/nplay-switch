# Nplay Switch 0.12.36

- Dublado/Legendado da conta agora determina o audio ao abrir outra obra. Uma
  escolha antiga salva no aparelho nao abre ingles/japones quando ha PT-BR e
  Dublado esta selecionado. Tanto faz continua usando o idioma salvo.
- Escolha manual durante o video permanece preservada na mesma reproducao,
  inclusive durante recuperacao de rede e troca de faixa.
- Voltar por toque usa a posicao real da acao no cabecalho. Corrige Serie,
  Anime, Dorama, Filme, Saga, Perfis e Configuracoes. A retomada tambem permite
  tocar em Cancelar. O retorno preserva o contexto de busca/lista.
- Auditamos as pendencias de legendas, reserva de pacotes, chamadas sincronas,
  fila de audio e organizacao do repositorio em
  docs/AUDIT_AUDIO_SUBTITLES_TOUCH_2026_10_01.md.

Build limpo ARM64 com -Werror e suite completa validate_release.ps1 passaram.
O teste de idioma reproduziu o defeito antes da correcao. Touch foi simulado
com tremor, tap, arraste, dois dedos e hit-test do cabecalho; HLS multifaixa
decodificou duas legendas no inicio e depois de seek.

Nplay.nro: 24.184.655 bytes. SHA-256:
8f78205f56a2380644b0876a7bab92363af0037c222ca1f10b8f9ed445361b73.

Esta release nao implanta a correcao do empacotador de legendas no servidor nem
reprocessa pacotes antigos. Disponibilidade de legenda real e touch no console
continuam exigindo verificacao no Switch fisico.
