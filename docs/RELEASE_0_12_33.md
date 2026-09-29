# Nplay Switch 0.12.33

## Perfil mais visivel

- A foto do perfil ativo no cabecalho cresceu de 46 para 70 px.
- O retrato ocupa a altura util do cabecalho sem aumentar a barra ou reduzir o
  espaco das capas.
- Aro azul mais legivel e indicador discreto de sessao ativa.
- Busca ganhou respiro em relacao ao avatar, preservando a navegacao existente.
- Menu rapido mostra a foto ativa com 86 px e texto realinhado.

## Desempenho e identidade visual

- Continua usando o recorte circular acelerado pela GPU introduzido na 0.12.32.
- Nenhuma textura, superficie ou mascara temporaria e criada por quadro.
- Nenhuma consulta de rede nova foi adicionada ao cabecalho.
- Altura da topbar, prateleiras, hit-tests e fluxo de navegacao foram preservados.

## Validacao

- Build Nintendo Switch ARM64 com warnings tratados como erro.
- `tools/validate_release.ps1` completo.
- Protecoes estaticas para tamanho, posicao, aro e indicador do avatar.
- Artefato: `Nplay.nro`, 24.172.367 bytes, SHA-256
  `70d9527cf5f1b2277a592c8cc1117912ee91815258b688b4646300d03ca0ec3a`.
- Pendente fisico: confirmar overscan em modo portatil e dock.
