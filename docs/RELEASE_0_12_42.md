# Nplay Switch 0.12.42 — preservar posição ao reconectar

- Removido o fallback que recomeçava automaticamente a obra quando a retomada
  após seek falhava. A recuperação conserva o ponto solicitado.
- Seek recusado ou fonte sem suporte a retomada não pode tocar desde o início
  silenciosamente. Falha de recuperação preserva a posição no resultado.
- Pausa, menus e prévia de seek não contam como tempo de falha de rede.
- Corrigida retomada de posições próximas ao começo e ao fim.

Supervisor C real testado com falhas simuladas: o código antigo perdeu a posição,
o corrigido passou. Compilação ARM64 e suíte completa local passaram. Ainda falta
confirmar os controles e a retomada no Switch físico; rede lenta pode gerar espera.

Atualize em Configurações e reinicie. Ou substitua Nplay.nro na pasta switch
da microSD. L/R/ZL/ZR abrem prévia: A confirma; B cancela.

SHA-256: `29cb1dbdff768f1adcd3f27f5ad189ebed0aaa5d49ea8c8a2aec378a2cb218db`
