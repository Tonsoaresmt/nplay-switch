# Nplay Switch 0.12.61 — Continuidade de episodios

- O proximo episodio continua iniciando automaticamente: com **Configuracoes >
  Reproducao > Proximo episodio automatico** ligado, a contagem de cinco segundos
  avanca sem apertar A. B/toque em cancelar continuam interrompendo.
- Oscilacoes na consulta do ponto salvo recebem novas tentativas automaticas,
  limitadas, sem recriar a sessao nem comecar do zero.
- Quando o servidor informa explicitamente que esta atualizando ANTES de admitir
  o play, o app espera e tenta novamente automaticamente, ate tres vezes.
- Erro persistente depois dos limites de recuperacao mantem o titulo/episodio
  escolhido na tela com Tentar novamente/Voltar, em vez de sair silenciosamente.
  Cancelar uma abertura nao e tratado como falha nem pede retry.
- Pular abertura mantem a posicao central, acima da timeline e separado das
  legendas; testes de toque, ocultacao e margens ampliados.

Nao repete automaticamente POST cujo resultado seja desconhecido (timeout,
resposta perdida ou 503 generico), nao pula episodios com erro nem marca EOF falso.
Fontes indisponiveis e falhas persistentes ainda podem exigir uma acao. Nao muda
preferencias da conta, backend, TLS, decoder ou o contrato de sessao.

Build ARM64 completo -Werror e bateria FINAL COMPLETA aprovados, sem pular
fixtures de midia. Inclui recuperacao, 1.000 ciclos de ownership, contagem nativa
sem input, 55 cenarios de midia e1.087blocosAAC. Teste fisico no Switch pendente;
nao e garantia de corrigir crash Horizon/NVTEGRA sem o trace daquele incidente.

NRO:24.311.631bytes. SHA256:
`a3df1de296145f9f9b4e1f45382ab92cbead22ba0aebcdd8adc6683aacb292c9`.
