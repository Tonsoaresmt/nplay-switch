# Nplay Switch 0.12.58 — sincronizacao na retomada

- Corrige o descarte de trechos de audio durante o preparo apos Continuar/seek.
- Conserva o ponto minimo de audio depois do primeiro quadro e corta amostras
  antigas/sobrepostas, evitando que o som retome em outro momento do video.
- Mantem o fim real da fila de audio ao fechar menus. Resampler e linha do
  tempo sao reiniciados apenas nos seeks confirmados, nao na pausa simples.
- Som apos seek so e liberado depois da imagem; pausar/despausar, fechar
  menus ou cancelar timeline durante o preparo nao contorna essa protecao.
- Backpressure conserva pacotes comprimidos; PCM tem teto de 4 segundos.
  Lacunas pequenas usam silencio. Lacuna acima de 2 segundos ou teto excedido
  gera recuperacao explicita no mesmo ponto, nao sucesso falso/reinicio zero.
- Diagnosticos de sincronizacao ampliados, sem URLs ou credenciais.

Build ARM64 `-Werror` e bateria completa local aprovados. Dois defeitos da .57
reproduzidos antes da correcao; 400 epocas/40 mil blocos simulados e 1087
blocos com timestamps AAC realmente decodificados testados. As suites
anteriores de controle, legendas, rede e midia continuam passando.

Versao destinada a teste no console apos validacao local. O episodio de
The Big Bang Theory nao foi testado no
Switch fisico: faltam temporada/episodio, versao instalada e logs recentes.
Nao afirmar resolucao definitiva desse incidente. Fontes com grandes lacunas
legitimas de PTS ou origem HLS desconhecida continuam exigindo investigacao.

NRO: 24.299.343 bytes. SHA-256:
`9a7b052875d6fdc5ddac96b4bdccf39b10ecc9955b13a09109fe316a320c3cab`.
Relatorio: `docs/AV_RESUME_SYNC_0_12_58.md`.

Publicada em 30/10/2026, 18:45 (Brasilia), como ultima versao do atualizador.
Tag/build: `93c4527b5b58fed4dbad227a5480cd5c9619d88e`.
Download publico integral e SHA-256 reconferidos apos a publicacao.
