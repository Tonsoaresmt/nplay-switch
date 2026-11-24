# Nplay Switch 0.12.63

## Correcoes

- Encerramento identifica a reproducao exata; uma resposta antiga nao deve
  fechar o novo play da mesma obra. Novas aberturas recebem sessao nova.
- Retorno ao login descarta respostas/caches antigos depois de encerrar seus
  trabalhos, preservando a identidade persistida do Switch no SD.
- QR no celular pode autorizar o console sem cadastrar o celular como outro
  aparelho. O mesmo Switch e reutilizado; aparelhos novos respeitam o plano.
- Legenda automatica pode trocar PT parcial pela completa quando chega depois,
  sem sobrescrever escolha manual ou OFF. Audiodescricao HLS e identificada.
- Melhor contrapressao de audio no fim do video e apresentacao de quadros VFR
  no prazo correto, mantendo controles e legendas durante a espera.
- Respostas HTTP recebem limites por rota; URL longa/truncada e rejeitada.

Dependencia: servidor Nplay com commit1c60ef4f ou posterior, a confirmar antes
de publicar esta release. Atualizar o NRO nao implanta o servidor.

## Validacao e limites

Build integral ARM64 com -Werror e bateria COMPLETA sem SkipMediaFixtures
passaram no mesmo NRO final: 55 cenarios de midia, 6 perfis, 118 HTTP locais,
1087 blocos AAC, concorrencia e regressao de legenda/login/sessao. Backend:
npm test completo, npm run check e testes HTTP/JWT/SQLite passaram.

Nao e homologacao em Switch fisico: GPU/NVTEGRA, sono/Wi-Fi e sessao longa
continuam exigindo teste no console. Registros muito antigos sem identidade
do aparelho podem precisar de regularizacao explicita; nao foram fundidos
automaticamente por nome/IP nem removidos aparelhos dos clientes.

Substitua somente Nplay.nro; mantenha os arquivos de configuracao e identidade
do SD. Artefatos sao distribuidos na release, nao no historico Git.

Nplay.nro: 24319823 bytes

SHA256: 4f287b3a1df2b22900ba2eb79a719e5be943039c710ac13a02f9bc1f20271002
