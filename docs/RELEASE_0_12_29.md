# Nplay Switch 0.12.29

Auditoria ponta a ponta do ciclo de vida do player, com foco em recuperacao e
saida responsiva.

## Recuperacao realmente responsiva

- Renovar sessao, consultar variantes, trocar fonte e resolver a nova URL agora
  ocorre numa thread propria.
- A animacao continua sendo desenhada durante DNS, TLS e HTTP.
- `B` ou `-` cancela a chamada em andamento, em vez de esperar ate 8 segundos
  por tentativa.
- O motivo seguro retornado pela API e preservado no diagnostico do player.

## Saida sem espera acumulada

- Heartbeat, progresso final e encerramento da sessao compartilham um worker
  cancelavel.
- A interface espera no maximo 1.200 ms; depois cancela o I/O pendente e conclui
  a limpeza local.
- O POST final duplicado foi removido e uma posicao ja salva nao e reenviada.
- Proximo episodio, voltar e erro usam a mesma politica de encerramento.

## Validacao

- Build ARM64 limpo com `-Wall -Wextra -Werror`.
- Novo `test_player_sync` para limites e deduplicacao.
- Consulte `PLAYER_END_TO_END_AUDIT_0_12_29.md` para a matriz completa e os
  testes ainda obrigatorios no Nintendo Switch real.
- Artefato: `Nplay.nro`, 24.160.079 bytes, SHA-256
  `16e67bfaa630183d6be8a929229011dc95d854b8deeda35ca389bb8fb074fffc`.
