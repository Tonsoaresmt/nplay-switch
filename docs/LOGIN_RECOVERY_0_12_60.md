# Recuperacao de entrada — 0.12.60

Nota de continuidade13/11: usuario confirmou acesso restabelecido, atribuindo
o bloqueio a uma atualizacao do Nintendo. Este documento descreve a etapa
INICIAL da .60 local, nao prova de causa daquele incidente. O candidato foi
ampliado; artefato/testes atuais em `DEVICE_IDENTITY_INTRO_0_12_60.md`. Nao
publicar o artefato historico3c16... abaixo como se fosse a candidata final.

12/11/2026 (Brasilia). Base `0ae4620`, checkout/branch hardening autorizados.
Relato: nao consegue acessar no Switch; mensagem, versao instalada e logs desta
tentativa ainda nao recebidos. Nao afirmar que a causa do incidente foi provada.

## Verificacao publica

Em 13/11/2026 02:49–02:51 UTC (12/11 23:49–23:51 Brasilia):

- Home e `/api/health`: HTTP 200; banco ok; worker recente.
- Producao identifica commit `d086dea2bd2f620b8802197b617320af984a0105`.
- `/api/auth/service-mode`: modo normal, sem manutencao ativa.
- Mesmo User-Agent do NRO, HTTP/1.1 e TLS limitado a 1.2: health 200.
- Bundle EXATO `data/cacert.bin` com libcurl/OpenSSL no PC: health 200,
  verificacao TLS 0. Isto NAO executa o SSL Horizon nem comprova rede do console.
- `/api/account/profiles` sem credenciais: 401 `Nao autenticado`, esperado.
- POST de validacao com corpo vazio em `/api/auth/login` e `/api/device/token`:
  400, respostas esperadas; sem tentativa de senha, conta ou sessao criada.
- Latest publica conferida: v0.12.59, NRO/hash iguais aos publicados.

Uma verificacao publica nao testa a conta do usuario nem exclui uma falha
intermitente, certificado no Horizon, relogio local, DNS ou rede daquele console.
O snapshot `build/site-c9d0f77` usado pelo validador continua HISTORICO; nao
representa o deploy atual. O codigo de auth/devices/server do commit publico
acima foi consultado no GitHub, sem acessar segredos/contas ou alterar servidor.

## Defeito demonstrado e correcao

O servidor emite 401 `Nao autenticado` quando `jwtVerify()` rejeita o token.
`reason=expired` e um caso diferente: vencimento do acesso da conta.
A .59 reconhecia apenas o segundo caso. No boot, perfis recusados por JWT
invalido voltavam para a lista de perfis, sem uma sessao valida para recarrega-la.
Era possivel sair com B, mas A apenas repetia a mesma consulta recusada.

`CatalogFetch.authentication_required` agora registra 401 de uma consulta
autenticada nao cancelada. Depois de join, somente o resultado aceito pela tela
ativa pode voltar ao login, com `Sua sessao terminou. Entre novamente.`
Expiracao da assinatura conserva seu aviso distinto. Resultado substituido,
cancelado/descartado ou de tela antiga nao encerra a conta. O fluxo invalida o
autoavanco pendente. Somente sessao local/usuario/perfil ativo sao limpos;
preferencias, listas e dados do SD nao sao apagados.

HTTP 403/429/5xx e falhas de DNS, conexao, timeout, TLS 60/77 ou cancelamento
NAO causam essa limpeza. Nenhuma verificacao de certificado foi relaxada.
Player, ffmpeg vendorizado, bundle CA, rotas, plano e backend nao foram alterados.

## Testes

`tools/test_login_recovery.mjs` compila worker CatalogFetch C real, parser cJSON
real, rotina de limpeza e despacho real anterior a aplicacao da resposta da UI.
Pthreads reais; HTTP, persistencia e SDL simulados. 2.200 requests/join em 100
rodadas, cobrindo 401 generico/com/sem JSON, vencimento, sem bearer, TLS/rede,
403/429/5xx, cancelamento, tela abandonada, descarte e resposta nova substituindo
401 antigo. Confere limpeza da copia de bearer e execucao da limpeza na UI.

`--baseline-059` carrega worker/despacho historicos d936f7a; a assertion de
retorno ao login FALHA, como esperado. Nao e teste verde e nao faz parte do
validador. A versao corrigida passa. Teste positivo novo e obrigatorio no release.
TLS CA e parser/poll de pareamento tambem passaram isoladamente.
Tentativa inicial de teste TLS nao iniciou GCC por restricao MSYS do sandbox;
nova execucao com aprovacao passou. Um nome incorreto de runner de pareamento
foi substituido pela compilacao C oficial usada no validador.

Build ARM64 completo `make -B -j4` com -Werror passou. Bateria FINAL COMPLETA
`tools/validate_release.ps1 -SkipBuild`, SEM SkipMediaFixtures, passou no NRO
final abaixo: login, TLS, worker/player, legendas, controles, 55 casos de midia,
1.087 blocos AAC reais e simbolos ELF. Backend de contratos e snapshot historico
c9d0f77, nao simulacao do servidor atual. Nenhuma mudanca do app/rebuild depois
da prova. `git diff --check` passou; sidecar local atualizado somente apos prova.

NRO 24.299.343 bytes, SHA-256:
`3c16d14d4847ba89e1ad0c2cc67be57b063cf5e5c1202705ff1ff843a813e662`.

Sem Switch fisico, login autenticado em producao ou prova do sintoma atual do
usuario. Nenhuma publicacao/commit/push/deploy nesta rodada. Solicitada
autorizacao para publicar .60 apos validacao; ainda sem resposta. .59 continua
latest. NRO/sidecar nunca commitar. Esta correcao nao corrige genericamente
todos os erros de entrada, especialmente erro TLS que ainda nao foi relatado.

## Verificacao no console

Se aparecer `Nao autenticado`/perfis vazios, B > Sair da conta e entrar novamente
e um contorno da .59, sem apagar arquivos. Na .60 a recusa deve oferecer o login
automaticamente. Se aparecer TLS 60/77, timeout ou outro erro, registrar a frase
completa/foto e traces novos: esta correcao NAO e uma promessa de corrigir TLS.
Nao apagar configs, perfis ou conteudo do SD para investigar.
