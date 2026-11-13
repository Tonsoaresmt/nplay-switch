# Candidato de servidor — identidade do Switch

## Atualizacao em 13/11/2026: porte local concluido, sem deploy

O usuario confirmou QR pelo celular e autorizou integrar/validar no site,
explicitamente sem deploy. Implementacao ativa no worktree backend
`C:/NplaySwitch/.codex-tmp/backend-device-identity`, branch
`codex/switch-device-reuse-20261113`, sobre `origin/main` d086dea2.
Suite geral `npm test`, `npm run check` e testes HTTP/JWT/billing/SQLite reais,
admin, migracao antiga e expiracao passaram. Nao houve push/deploy nem teste
fisico. Detalhes em `docs/SWITCH_DEVICE_IDENTITY_2026_11_13.md` daquele worktree.

Os dois arquivos JS daqui ficam como **prototipo historico** do teste local do
NRO, nao sao o patch final para aplicar. O porte usa o migrador oficial, valida
acesso atual da conta e compartilha a aprovacao administrativa. Nao sobrescrever
os arquivos novos do backend com estas copias. As instrucoes abaixo descrevem
a fase anterior ao porte. .59 continua publicada e .60 permanece local.

NAO implantado. Estes arquivos nao entram no NRO e nao alteram o servidor por
estarem neste repositorio. Sao uma correcao preparada para portar ao repositorio
`Tonsoaresmt/Nplay`, nao um segundo servidor nem uma configuracao do console.

Base conferida no GitHub: `d086dea2bd2f620b8802197b617320af984a0105`.
A rota original `src/routes/devices.js` corresponde byte a byte, ignorando CR,
ao snapshot autorizado c9d0f77 usado na reproducao/teste local.

## Portar e validar antes de qualquer deploy

1. Revisar as mudancas atuais do backend e suas instrucoes; preservar trabalho
   de outras pessoas. Nao substituir um arquivo mais recente automaticamente.
2. Portar `pairing-device.js` para `src/lib/pairing-device.js` e as alteracoes
   de `devices.js` para `src/routes/devices.js`.
3. A instalacao da rota acrescenta `device_fingerprint TEXT`, em transacao,
   apenas se a coluna nao existir. Nao modifica aparelhos ou pairings antigos,
   nao limpa duplicatas e nao muda o limite do plano. Nao ocultar falha de DDL.
   Pode-se mover esta instalacao para o migrador oficial durante o porte.
4. Rodar testes completos do backend, inclusive pareamento, auth, billing,
   migracao de banco antigo/novo e preHandlers Fastify reais. O teste deste
   repositorio usa SQLite real, mas simula esses modulos de infraestrutura.
5. Somente apos autorizacao/validacao, publicar o backend e confirmar o health
   com o commit efetivamente implantado. Nao declarar suporte antes do deploy.

## O que corrige

O NRO passa o MESMO `device_fingerprint` do login por senha na criacao de um QR.
Um codigo novo continua secreto e temporario: fingerprint nao e um token de
login, nao escolhe o usuario e so pode reutilizar um aparelho APOS a aprovacao
autenticada daquela conta. Consulta limitada a user_id+fingerprint+approved.

Na aprovacao, resolve o aparelho existente ANTES de conferir vagas novas. Se
for realmente novo, reserva a vaga na mesma transacao que aprova o codigo.
Isso evita duas aprovacoes consumirem a mesma ultima vaga antes do polling.
O token confirma novamente vinculo/conta e nao recria aparelho removido.
Criacao/reutilizacao/remocao invalida o cache de autenticacao da conta.

Nao reutilizar por nome, IP/NAT, modelo ou somente por haver um unico Switch.
Registros legados de QR sem fingerprint nao provam ser o mesmo console; nao
fazer fusao/apagamento automaticos. Precisam de uma vinculacao explicita unica
ou liberacao manual de vaga. Depois de cadastro com identidade, os proximos
QR/senha devem reaproveitar o mesmo ID. Duplicatas ja existentes nao sao limpas.

Pareamentos ja aprovados pelo servidor antigo, mas ainda sem device_id, pedem
novo codigo apos o deploy em vez de recriar uma vaga sem reserva/consentimento.
Codigos novos duram os mesmos 10 minutos; limites/polling/JWT foram preservados.

## Prova local

`node tools/test_pairing_identity_api.mjs` reproduz 409 com a rota original
quando o mesmo Switch ja ocupa a unica vaga. O candidato passa reaproveitamento
no limite/30 novos QR, segunda vaga simultaneamente reservada, isolamento entre
contas/NAT/nome, clientes sem fingerprint, revogacao, rollback e esquema aditivo.
Nenhuma conta ou banco de producao foi usado. Nao e prova de deploy/hardware.

Referencia de seguranca do fluxo: [RFC 8628](https://www.rfc-editor.org/rfc/rfc8628.html).
