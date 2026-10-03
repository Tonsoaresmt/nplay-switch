# Nplay Switch 0.12.43 — player mais resistente a interrupções

- Detector de conexão parada passa a distinguir espera por buffer cheio de falta
  real de dados, reduzindo risco de timeout após pausa e menus.
- Seek com leitura ocupada não reabre o player inteiro: conserva a prévia para
  tentar novamente ou cancelar, sem perder a posição.
- Após buffering, pequena reserva de vídeo antes de continuar, dentro do mesmo
  limite de memória e com prazo máximo.
- Diagnóstico melhorado para identificar idle e reserva, sem expor URLs secretas.

Compilação ARM64 e suíte local completa passaram, incluindo teste com libcurl
real em localhost e callbacks/worker/supervisor C. Ganho de fluidez no Switch
físico ainda precisa de confirmação; rede lenta continua podendo causar espera.

Atualize em Configurações e reinicie, ou substitua Nplay.nro na pasta switch.
L/R/ZL/ZR abrem prévia: A confirma e B cancela.

SHA-256: `3b3871db55107c100c54d2b66bbedf64518089b869812b26ba251ccb875acfeb`
