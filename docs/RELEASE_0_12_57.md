# Nplay Switch 0.12.57 — candidata local

- Protege o upload quando a resolucao do quadro muda: planos menores nao
  sao enviados diretamente para uma textura maior. A conversao conserva
  o tamanho reservado, sem crescimento a cada mudanca de resolucao.
- Fallback de NV12 para IYUV e reaproveitado; renderer sem NV12 deixa de
  tentar criar e destruir texturas a cada quadro.
- Proporcao considera pixels nao quadrados e o tamanho atual do quadro.
  Letreiros acompanham o retangulo do video. Erros de conversao sao tratados.
- Busca em clipes com duracao conhecida de ate1s nao salta para fora deles.
- Preserva controles e analogico direito opcional OFF por perfil da .56,
  e correcoes de seek/retomada TorBox/R2/Range da .55.

Validacao local completa aprovada: ARM64 `-Werror`,200000 passos simulados,
1152 transicoes de upload instrumentadas e55 cenarios de midia real via
FFmpeg/HTTP local (360p a1080p60, retrato, audio mono/stereo/5.1).
Inclui testes de pausa/retomada/faixas/legendas/episodios/TLS e falhas de rede.

Nao e homologacao fisica: CLI usa FFmpeg8.1.1, NRO7.1; GPU/NVTEGRA, rede
domestica, producao TorBox/R2 e sessao longa precisam de teste no console.
Normalizar resolucao variavel tem custo de CPU a medir. Fonte sequencial
legada sem contrato de busca continua sem suporte a saltos.

NRO local:24.291.151bytes. SHA-256:
`85ade00f99127a22600fbdfd8e629d3df426629dc4af93d996dd264779e63e23`.
Nao publicada no atualizador. Relatorio:
`docs/PLAYER_SCENARIO_MATRIX_0_12_57.md`.
