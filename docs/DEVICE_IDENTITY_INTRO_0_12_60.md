# Identidade de aparelho e Pular abertura — candidato 0.12.60

13/11/2026. Mesmo checkout `switch-access-expiry`, branch hardening.
As mudancas locais da rodada anterior foram preservadas e incorporadas;
nenhuma release .60 foi publicada. Usuario confirmou acesso restabelecido e
atribuiu o bloqueio anterior a uma atualizacao do Nintendo. Isso NAO comprova
que o defeito JWT corrigido localmente tenha causado aquele incidente.

## Cadastro duplicado: causa demonstravel

O login com senha envia um fingerprint salvo em `device_id.txt`. O QR anterior
enviava so nome/tipo, sem identidade persistente. A rota atual do site confere
o total de dispositivos na aprovacao e cria outro registro no polling do token.
Mesmo nome/IP nao faz o servidor reconhecer o mesmo aparelho. Tambem um login
por senha depois de um QR legado sem fingerprint parece um aparelho novo.

A rota consultada em `Tonsoaresmt/Nplay`, commit
`d086dea2bd2f620b8802197b617320af984a0105`, e identica (ignorando CR) a fixture
local c9d0f77 para devices.js. Com SQLite temporario e essa rota real,
reproduzido HTTP409 para o proprio Switch ocupando a unica vaga.
Nao foi consultada a conta do usuario, nem confirmado se ele usou QR ou senha.

### Correcao no NRO

- QR e senha usam `ensure_device_identity` e a mesma identidade por instalacao,
  nao uma identidade por perfil/conta ou um identificador do hardware.
- QR envia `device_fingerprint`, opcional/retrocompativel com servidor antigo.
- Arquivo existente e validado integralmente, sem NUL embutido/truncamento.
  Falha de leitura nao e confundida com arquivo inexistente.
- Identidade nova e publicada por escrita completa/flush/close/rename e releitura.
  Se nao puder persistir, o app nao faz login com identidade efemera. Nao troca
  um ID valido, nao o remove no logout e nao apaga arquivos do usuario.
- Falhas de SD/corrupcao exigem cuidado do usuario, nao rotacao silenciosa.
  10.000 releituras/entradas conservam a mesma identidade no teste C.

### Correcao de servidor: preparada, NAO implantada

`integration/nplay-device-identity/` contem candidato e roteiro para portar ao
repositorio do site. Nao e compilado no NRO nem altera producao automaticamente.
Somente o NRO novo nao resolve o backend antigo ignorando o fingerprint.

O candidato acrescenta uma coluna nullable aos pairings e reutiliza por
usuario+fingerprint+approved ANTES da verificacao de vaga. Somente apos aprovacao
autenticada daquela conta. Nao deduz identidade por IP, modelo ou nome.
Reserva vaga nova e aprova na mesma transacao; polling confirma o vinculo.
Remover aparelho revoga o codigo ja aprovado: polling nao o ressuscita.
Invalida cache de autenticacao em criacao/reuso/remocao. Nao aumenta limites,
nao apaga duplicatas e nao escolhe um registro legado sem prova de identidade.

SQLite real: rota antiga reproduz409; candidato passa30 novos QR do mesmo
aparelho no limite, senha com mesmo fingerprint, reserva da ultima vaga,
mesmo NAT/nome/conta diferente, fingerprint ausente/invalido, codigo expirado,
revogacao/ownership, rollback e esquema aditivo. HTTP/Fastify/JWT/billing/QR
sao simulados. Precisa de integracao e suite REAL do backend antes de deploy.
Solicitada autorizacao para portar/validar no repo do site; ainda sem resposta.

Registros antigos sem fingerprint nao sao magicamente reconhecidos. Requerem
vinculacao explicita ou liberacao manual uma vez. Um codigo antigo aprovado mas
nao reclamado antes do deploy pode precisar ser gerado de novo (duracao10min).

## Pular abertura/encerramento no NRO

O NRO nao tinha essa funcao. O site tem codigo JS proprio, nao executado pelo
homebrew SDL. Foi integrada uma implementacao nativa, mantendo a pipeline atual.

- Somente episodios, depois do primeiro quadro e com duracao real60s–4h.
- Um worker opcional consulta `/api/stream/:id/segments` com item/fonte/duracao,
  autenticao e perfil existentes; nao cria sessao, pedido ou download de video.
- API com corpo<=32KiB, conexao2s/total5s. Se a API der fallback publico valido,
  consulta o host FIXO TheIntroDB sem bearer/cookie/redirecionamento, TLS verificado,
  corpo<=32KiB, ate3s dentro da janela global8s. Nunca usa URL recebida do servidor.
- Valida numeros/identidade remota/tipo/intervalos. Abertura5–240s e antes do
  final; encerramento5–300s no quarto final/ultimos5min. Conflitos nao escolhem
  uma marcacao arbitraria. Sem marcacao/404/timeout/401: video segue sem botao.
- Cancelamento proprio; nao usa demux_abort. Resultado somente apos done/join,
  conferindo item/fonte/duracao. Teardown cancela/junta antes de liberar a memoria.
- Uma consulta por tentativa. Seek/reopen pode pedir de novo (cache do servidor);
  nao foi criado cache persistente sem validade. Nenhum timer/retry infinito.
- Botao centrado em x640, retangulo380x68 em450/508, acima da timeline600.
  Mesma geometria para desenho e hit-test. Falas reservam espaco acima do botao;
  menus/busca/buffering ocultam-no. Texturas/fontes existentes, sem novos assets.
- Direita seleciona o botao apenas durante a marcacao; A confirma, B/esquerda
  cancelam a selecao. Fora da marcacao, Direita conserva Proximo episodio.
  A sem selecao continua pausando normalmente. Nao rouba foco automaticamente.
- Touch captura um dedo e executa apenas ao soltar dentro do alvo ainda valido.
  Gesto para fora, outro dedo e intervalo expirado nao executam.
- Salto reutiliza o caminho da timeline, preservando pausa/checkpoint/faixas,
  incluindo reabertura posicionada HLS e contrato restart do TorBox.
  Fonte sem seek nao mostra o botao. Busca ocupada oferece repetir/cancelar.
- Encerramento deixa o ultimo segundo rodar: buscar exatamenteEOF nao teria
  primeiro quadro para apresentar. Nao inventa conclusao nem autoplay.

Cobertura depende de metadados corretos para aquele episodio/release; nao ha
fallback fixo90s nem detecao automatica pelo som. Precisao visual deve ser
conferida no Switch. Provedor pode estar indisponivel, sobretudo fora do cache.
Teardown ainda precisa de join e pode aguardar cancelamento/timeout da rede;
nao prometer ausencia total de latencia. Consumo real da thread/GPU requer console.

## Validacao e limites

- `test_device_identity`: store/ensure C reais, filesystem host + falhas injetadas,
  10.000 reusos, escrita/flush/close/rename/leitura, NUL e tamanhos invalidos.
- `test_intro_skip`: modulo/worker C reais,212 ciclos pthread/join, HTTP simulado,
  validacao/conflitos, fallback/bounds/erros, cancel/resposta antiga, duracao,
  confirmacao pausada, targetEOF seguro, captura touch e desenho central extraidos.
- `test_pairing_identity_api`: SQLite real e roteamento/helper reais; infraestrutura
  Fastify/billing/JWT simulada. Baseline e uma reproducao NEGATIVA, nao teste verde.
- Controles200k passos e navegacao/touch preexistentes passaram isoladamente.
- Build ARM64 COMPLETO -Werror + incremental final passaram. A primeira bateria
  completou durante o ajuste de EOF e NAO e usada como prova do artefato final.
  Bateria FINAL COMPLETA `validate_release.ps1 -SkipBuild`, sem SkipMediaFixtures,
  PASSOU depois da ultima compilacao, no binario imutavel abaixo. Incluiu todos
  os testes novos, 55 casos de midia/6perfis/118requests, 1.087 blocos AAC reais,
  controles, legendas, TLS, retorno HOME e simbolos ELF. Snapshot de contrato
  backend c9 e historico, nao teste de conta/deploy atuais.
  `node --check` dos dois arquivos candidatos de servidor e `git diff --check`
  passaram. Nenhuma mudanca de app/rebuild apos a prova FINAL.

FINAL local atual:24.311.631bytes; SHA256
`0c1a5a6208109ec97e21abc13f7ea80370c25f58a6e58a1e3cf4cf3c9ff87157`.
Sidecar local atualizado apos conclusao da bateria. NRO/sidecar nunca commitar.
Sem Switch/Horizon/NVTEGRA/SD fisico, conta ou deploy de producao. Nao publicar
sem autorizacao. .59 continua latest; o candidato de backend nao esta no ar.

## Atualizacao: integracao local do backend (13/11/2026)

Usuario confirmou QR pelo celular e autorizou integrar e validar SEM deploy.
Porte concluido no worktree `C:/NplaySwitch/.codex-tmp/backend-device-identity`,
branch `codex/switch-device-reuse-20261113`, base `origin/main` d086dea2.
Suite geral `npm test`, `npm run check`, pairing HTTP/Fastify/JWT/senha/billing/
SQLite reais, aprovacao admin e migracao de banco antigo passaram. O novo teste
tambem verifica quota concorrente, rollback, ownership, acesso revogado e cache.
Nenhum banco/conta de producao, deploy ou Switch fisico foi usado.

Migracao e aprovacao administrativa foram integradas ao fluxo oficial; os dois
JS de integration/ sao apenas prototipo historico. Ver README e o relatorio
`docs/SWITCH_DEVICE_IDENTITY_2026_11_13.md` no backend. Sem commit/push/publicacao
nesta rodada. NRO/hash/testes acima nao mudaram; .60 continua local, .59 publicada.
Registros QR legados sem identidade ainda precisam de primeiro vinculo explicito
ou liberacao manual da vaga; atualizar somente o servidor nao corrige o QR .59.

## Fontes

- Site: `docs/INTRO_SKIP_PLAYER.md`, `public/js/player/intro-skip.js`,
  `src/services/intro-segments.js`, `src/routes/auth.js` e `src/routes/devices.js`,
  revisao d086dea. A documentacao do site confirma que NRO precisava de integracao.
- [RFC8628](https://www.rfc-editor.org/rfc/rfc8628.html): autorizacao de aparelho
  no telefone, codigo temporario, confirmacao/polling; fingerprint e extensao
  propria de identidade da instalacao, nao credencial substituta.
