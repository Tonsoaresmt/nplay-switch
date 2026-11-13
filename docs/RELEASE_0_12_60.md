# Nplay Switch 0.12.60 — Entrada e Pular abertura

- Corrige o retorno ao login quando o servidor recusa uma sessao antiga.
  Antes, o app podia ficar na tela de perfis repetindo `Nao autenticado`.
- Distingue sessao encerrada de acesso vencido da conta.
- Mantem a sessao em falhas de internet/certificado; nao apaga preferencias.
- Resultados antigos, substituidos ou cancelados nao desconectam o usuario.
- QR e senha passam a usar a mesma identidade persistente do Switch. Falha de
  SD nao gera login com identidade temporaria. Reutilizacao do cadastro por QR
  depende tambem da correcao correspondente do servidor, integrada e validada
  separadamente. Cadastros QR antigos sem identidade exigem primeiro vinculo
  explicito ou liberacao manual de vaga; nao sao apagados automaticamente.
- Pular abertura/encerramento nativo, quando ha tempos confiaveis para o episodio.
  Botao central, Direita/A/B ou touch, respeitando pausa e capacidade de busca.
  Falha ao consultar marcacoes nao interrompe o video.

Nao desativa a verificacao HTTPS nem altera limites dos planos. A correcao
correspondente do backend segue sua publicacao propria. O relato anterior de
acesso resolvido pelo usuario nao foi atribuido a JWT/TLS sem logs.

Publicacao autorizada pelo usuario em 13/11/2026. Compilacao ARM64 completa -Werror e
bateria completa sem pular fixtures passaram, incluindo 2.200 requests de
recuperacao do login, TLS, player, legendas e midia. Teste fisico pendente.

Novo NRO 24.311.631 bytes. SHA-256:
`0c1a5a6208109ec97e21abc13f7ea80370c25f58a6e58a1e3cf4cf3c9ff87157`.
Validacao FINAL COMPLETA desse candidato ampliado passou, sem pular fixtures,
no artefato acima. Nenhuma alteracao do app/rebuild apos a prova. Sem teste no
Switch fisico. Backend passou `npm run check`, `npm test` completo e testes de
QR/senha/admin com HTTP/JWT/billing/SQLite reais em banco temporario. Push e
deploy do servidor sao conferidos separadamente da release do NRO.
