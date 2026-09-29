# Nplay Switch 0.12.30

## Correcoes desta versao

- A ultima faixa de audio escolhida manualmente no player passa a vencer a
  preferencia geral da conta nas proximas obras e episodios.
- Uma versao Dublado ou Legendado escolhida explicitamente no detalhe da serie
  continua tendo prioridade. Assim, a memoria do player nao contradiz uma escolha
  consciente feita antes de abrir o episodio.
- Alterar a preferencia global em Configuracoes limpa a escolha manual antiga e
  faz a nova regra valer imediatamente nas proximas reproducoes.
- O diagnostico do player registra preferencia efetiva, escolha explicita e idioma
  salvo, sem registrar URL ou credencial.
- Quando o HLS realmente nao possui faixa de legenda, o painel explica que aquela
  versao da obra nao recebeu legendas, em vez de sugerir que o botao falhou.

## Causa das legendas ausentes

O NRO enumera as faixas publicadas no manifesto HLS. O teste local do player abre
um fixture com duas legendas WebVTT, decodifica cues antes e depois de seek e passa.
A captura de hardware mostrou apenas `Desligadas`, confirmando que o manifesto
da obra entregue pelo servidor continha zero rendicoes de legenda.

O preparador do backend reutilizava pacotes `multitrack-v3` criados antes da
publicacao de legendas, mesmo quando o `ffprobe` atual encontrava faixas de texto.
A correcao correspondente esta na branch `codex/switch-subtitles` do repositorio
`Tonsoaresmt/Nplay`: novo schema imutavel `multitrack-v4-subtitles`, validacao do
manifesto e falha explicita se nenhuma legenda de texto puder ser convertida.

Pacotes antigos nao ganham dados retroativamente. Eles precisam passar por
`Reparar`/repreparo controlado no servidor depois que a correcao do backend for
implantada. Legendas bitmap PGS nao podem ser convertidas com seguranca para texto
sem OCR e continuam fora deste fluxo.

## Validacao local

- `tools/validate_release.ps1`: compilacao do NRO com `-Werror` e todos os testes
  nativos do repositorio.
- Politica de audio: PT/EN/JA, escolha manual, preferencia de conta, continuidade e
  selecao explicita de versao.
- Fixture HLS: cinco faixas, duas legendas WebVTT, cues no inicio e depois de seek.
- Backend: `npm run check`, contrato HLS multifaixa e `npm run test:media-worker`.

Artefato local validado: `Nplay.nro`, 24.160.079 bytes, SHA-256
`4eb1ad838a5c1d9cc49205b316d3afc788f4a08f8dbe945a5197b984ea08dfea`.

O comportamento ainda precisa ser confirmado em Nintendo Switch real com uma obra
repreparada pelo backend novo. Compilacao e simulacao nao substituem esse teste.
