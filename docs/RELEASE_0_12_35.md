# Nplay Switch 0.12.35

## Entrada guiada para novos usuarios

- A tela vazia de login foi substituida por uma recepcao com duas rotas claras:
  conectar pelo celular (acao principal) ou usar usuario e senha (alternativa).
- O Switch solicita um codigo de dispositivo em uma thread separada. DNS, TLS,
  polling, oscilacao de rede e cancelamento nao bloqueiam desenho, toque ou Joy-Con.
- O QR Code e desenhado localmente como matriz binaria limitada; nao baixa imagem
  de terceiros e nunca contem o `device_code` secreto usado pelo polling.
- A tela mostra codigo digitavel, validade, tres passos, estado da conexao e deixa
  cancelar ou gerar outro codigo. Apos aprovacao, token e usuario sao persistidos
  e o fluxo segue para a escolha de perfil existente.
- O polling respeita `interval`, `slow_down` (+5 s), expira localmente e usa backoff
  limitado durante falhas transitorias. Respostas e matrizes malformadas sao
  rejeitadas sem expor segredo ou deixar a interface presa.
- Login tradicional permanece disponivel por `Y` e como segundo card. Touch usa
  hit-tests absolutos nos dois cards; nao simula movimentos de Joy-Con.

## Contrato do site/celular

- O backend devolve a matriz QR no proprio `/api/device/code`, aplica `no-store`,
  valida segredos/codigos e limita criacao e polling por IP.
- Login, cadastro e visitante preservam o codigo do Switch. A confirmacao no
  celular identifica o Nintendo Switch e explica que o console entrara sozinho.
- A regra comercial do visitante nao foi ampliada nesta release: producao ainda
  usa a previa protegida ja existente. Definir separadamente se “3 conteudos”
  significa tres titulos completos, tres previas, por dia ou por dispositivo.

## Validacao

- Build ARM64 `-Werror` e teste host do parser/polling QR.
- Teste Fastify cobre criar codigo, pendente, aprovar, emitir token, validacao e
  rate limit; teste web cobre URL/QR, expiracao, retry, `slow_down` e cancelamento.
- Auditoria estatica confere os 45 contratos HTTP entre NRO e backend.
- Validacao limpa concluida: `Nplay.nro` tem 24.184.655 bytes e SHA-256
  `4bf8a54555c38189898dc5b0297d6a3541ce25aee48e9c9db97db0f2a342eebc`.
- Backend publicado no `main` em `c650809` e implantado pela execucao
  `36797192004`. A API publica foi comprovada com HTTP 200, `Cache-Control:
  no-store` e matriz `bit-rows-v1` valida de 29 x 29 modulos antes da release.
- Pendente obrigatorio: instalar no Switch real, escanear em Android e iPhone,
  percorrer conta existente/cadastro/visitante, cancelar, expirar, desligar Wi-Fi
  e confirmar legibilidade em modo portatil e dock.
