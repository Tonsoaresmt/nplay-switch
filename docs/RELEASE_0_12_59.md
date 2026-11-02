# Nplay Switch 0.12.59 — TorBox e estabilidade

- HTTPS nativo com leitura, escrita e handshake cancelaveis e limitados por
  prazo; evita esperas internas sem retorno ao player.
- Video direto usa a mesma CA embutida do login/HLS, preservando verificacao
  de certificado, hostname e data. Falhas de importacao bloqueiam a conexao.
- Fila pode alcancar video depois de grandes rajadas de audio sem descartar
  amostras nem aumentar o teto de bytes; corridas de chegada/espaco tratadas.
- Fonte que excede o limite recebe aviso, preservando o ponto e sem ciclo
  de reconexao da mesma tentativa.
- Diagnosticos nativos e de reserva ampliados, sem URLs/credenciais.

Build ARM64 completo -Werror + incremental final aprovados. Bateria FINAL
completa aprovada, sem pular fixtures de midia: novos testes TLS/fila, 55
cenarios de midia e 1087 blocos AAC reais. Disponibilizada para teste no
console; ainda sem homologacao em um Switch real.

Nao ha confirmacao do episodio de VisionQuest relatado. Ainda necessaria
validacao fisica de R2/Range/remux, retomada, audio/legenda e HOME/sono.

NRO24.299.343B, SHA256:
`deed0e30de6c6576ff2f0c10f481262c1cf1016f07a511b9c2213b4223e472b9`.
Relatorio: docs/TORBOX_FIXES_0_12_59.md.

Publicada/latest em 01/11/2026, 23:39:47 (Brasilia).
Build/tag: `d936f7a707ab9fd2da11fe47dd6faa851c08fed9`.
Download publico integral, tamanho e SHA-256 reconferidos apos publicar.
