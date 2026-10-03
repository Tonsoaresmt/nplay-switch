# Limite de acesso e rotas de saída — 2026-10-03

## Regra aplicada

`access_expires_at` continua sendo decidido pelo servidor. Ao vencer, catálogo,
busca e abertura de reprodução recebem `401` com `reason=expired`. O Nplay Switch
agora reconhece esse contrato, remove somente a sessão local e leva a pessoa para
o login com uma mensagem explícita. Não há repetição infinita de reconexão.

Para mídia controlada pelo Nplay, a URL de entrega é limitada ao tempo restante
da conta e a rota de bytes reconsulta o estado da conta antes de cada range.
Isto cobre R2, hot e debrid. Uma URL externa que o dispositivo acessa diretamente
não pode ser cortada no meio sem uma camada controlada pelo Nplay; as próximas
aberturas continuam bloqueadas pelo servidor.

## Teste de hardware pendente

1. Usar uma conta de teste que expire em dois minutos.
2. Abrir um R2, um hot/debrid e navegar no catálogo antes do prazo.
3. Após vencer, solicitar um segmento novo ou tentar abrir outro título.
4. Confirmar `Acesso expirou`, retorno ao login e que B/toque continuam saindo
   das telas de carregamento.

Não publicar esta documentação como prova de teste em hardware: a validação local
confere contrato, compilação e regressões; o Switch físico continua necessário.
