# Auditoria das rotas de reproducao do Switch — 24/09/2026

Base analisada: cliente Switch 0.12.10 e API Nplay `origin/main` em `e2d10c6`.
O banco local foi consultado somente para leitura; seus numeros nao garantem
o estado atual da producao.

## Corrigido no cliente 0.12.11

- Detalhe de serie: quando dois episodios tinham progresso parcial, a UI
  selecionava o primeiro episodio na ordem do catalogo. Agora usa o
  `progress_updated_at` ja enviado pela API.
- Recuperacao de fonte: `/fail` fornece apenas `source_id` e `play_url`, mas
  a nova fonte pode ter outro container. O cliente nao reaproveita mais a
  classificacao da fonte anterior: consulta variantes antes de acionar a rota
  e so aceita uma nova resolucao completa que confirme o `source_id`.
- Progresso: a thread podia enviar o ponto salvo durante uma abertura sem
  primeiro quadro, atualizando o Historico e ate disparando prefetch de outro
  episodio. Agora heartbeat e progresso comecam apos a primeira apresentacao;
  uma tentativa sem quadro nao grava progresso ao encerrar.

## Riscos ainda no servidor

1. `POST /api/stream/session/:id/fail` desativa `item_sources.active` para
   todos os usuarios apos uma falha de um unico cliente. Mesmo com a protecao
   no Switch, outros clientes ainda podem chamar essa rota. O contrato ideal
   e excluir a fonte apenas da sessao que falhou e devolver um descriptor
   completo e tipado para a alternativa, sem desativacao global automatica.
2. `POST /api/stream/:itemId` reserva a sessao antes de concluir a resolucao
   remota de um embed de anime. Se essa resolucao retorna 503, a sessao fica
   ativa ate expirar (janela de 90 s), podendo consumir temporariamente uma
   tela de outra pessoa da mesma conta.
3. Se `refresh` falha antes de retornar um URL, o supervisor do Switch encerra
   apos as tentativas de renovacao. Chamar `/fail` automaticamente nesse caso
   ampliaria o risco do item 1; corrigir primeiro a semantica da rota no API.
4. `GET /api/play/:itemId` chama `resolveStreamUrl(itemId, srcId)`. Mesmo com
   `srcId` assinado, essa funcao pode usar outra fonte ativa se a escolhida
   falhar. O redirect pode entao entregar embed ou outro container, enquanto
   o cliente ainda espera o container e a entrega do descriptor original.
   Uma fonte explicitamente selecionada deve falhar com erro controlado ou
   devolver um novo descriptor tipado, nao trocar de formato no redirect.

No retrato local, 2.989 obras tinham apenas uma fonte R2 ativa e 417 tinham
R2 junto de embed ou torrent. Esses grupos tornam o preflight de alternativa
importante, mas nao substituem teste no console ou auditoria do banco atual.
