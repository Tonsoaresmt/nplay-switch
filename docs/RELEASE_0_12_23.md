# Nplay Switch 0.12.23

## Correcoes do player

- Seek agora tenta `avformat_seek_file` dentro de uma janela temporal, mantendo
  video, audio e legenda como streams ativos. Fontes incompatíveis usam o antigo
  `av_seek_frame` automaticamente.
- Trocas HLS de audio e legenda receberam limite de dez segundos.
- Pressionar `B` durante a sincronizacao cancela somente a troca e mantem a faixa
  anterior. O botao e consumido para nao fechar o player logo depois.
- Falha de abertura, seek, cancelamento ou timeout restaura decoder e disposicao
  da faixa anterior.
- O diagnostico passa a registrar falta de audio, fila SDL acima de 1,5 segundo,
  quantidade de trocas malsucedidas e maior tempo observado para uma troca.
- A tela de diagnostico exibe essas metricas sem precisar copiar arquivos da SD.

## Verificacao local

- Suite limpa completa concluida sem erros nem avisos.
- Contratos site/Switch, API de playback, relogio, politica de audio, fila de
  legendas, episodios, remux e simbolos obrigatorios aprovados.
- A validacao impede remover a janela de seek, o timeout cancelavel ou o rollback
  de faixa em alteracoes futuras.
- Artefato: `Nplay.nro`, 24.147.791 bytes,
  SHA-256 `84a0fd48b473264f084689217a5d0898018fb39eb329a3358d9f13587ba94851`.

## Testes ainda necessarios no Switch

- Cancelar uma troca mantendo `B` pressionado e continuar assistindo.
- Deixar uma rendition indisponivel ultrapassar dez segundos e confirmar rollback.
- Seek em filme HLS, episodio, anime MP4 e dorama de fonte lenta.
- Fotografar o diagnostico depois de um video fluido e depois de um engasgo para
  comparar faltas de audio, filas altas, leituras e pausas de apresentacao.
