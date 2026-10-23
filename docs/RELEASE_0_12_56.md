# Nplay Switch 0.12.56 — candidata local

- Nova opcao em Configuracoes > Reproducao: **Analogico direito: minutagem**.
  Desligada por padrao e salva por perfil neste Switch. Segurar abre a timeline;
  A confirma e B cancela. Soltar o analogico nao aplica o salto.
- L/R (10s) e ZL/ZR (60s) agora repetem ao segurar, acumulando o destino sem
  recarregar a cada passo. Soltar aplica uma vez; B cancela.
- Busca pendente nao confirma menus/faixas/episodios nem reaparece ao fechar
  outro painel. O esquerdo nao abre mais a timeline. Preview touch conserva
  a posicao de origem correta.
- Inclui as correcoes ainda locais da .55: seek TorBox por restart explicito,
  barreira segura HLS/R2, progresso/legenda com o tempo do trecho e melhorias
  de heartbeat/recuperacao. Fonte sequencial antiga sem suporte continua sem
  busca, com aviso; nao e seguro simular sucesso ou reiniciar em zero.

Compilacao ARM64 completa -Werror e bateria completa local passaram, incluindo
fixtures reais HTTP/FFmpeg. Os controles usam C real com SDL/GPU simulados;
nao houve teste em Switch fisico, VisionQuest ou TorBox de producao.

NRO local: 24.291.151 bytes. SHA-256:
`79e076184974431d3a4e765205809d7174f28551ee9698efe450314acce2d4d9`.

Ainda nao publicada: GitHub latest conferido nesta rodada e v0.12.54.
Relatorio, limites e roteiro fisico: `docs/SEEK_CONTROLS_0_12_56.md`.
