# Auditoria das telas e rotas do Nplay Switch — 25/09/2026

Base: cliente Switch 0.12.12 e registros de rota do backend `origin/main` em
`C:/iptv`. O backend local `main` esta atras do remoto; as verificacoes de
contrato usaram explicitamente `origin/main`. Os testes de handler existentes
rodaram em banco SQLite temporario e no checkout local. Nenhum teste gravou na
base de producao. O NRO nao pode ser executado no Switch real neste ambiente.

## Matriz de telas

| Tela/caminho | Entrada e saida conferidas | Dados e verificacao | Resultado |
| --- | --- | --- | --- |
| Login | A entra, + sai, erro volta ao formulario | `POST /api/auth/login` | Contrato estatico OK; hardware pendente |
| Perfis | Listagem, troca, B, toque em card | `GET /api/account/profiles` | Contrato estatico OK; hardware pendente |
| Inicio | Destaques, Continuar, rails, busca e retorno do detalhe | `/api/catalog/home`, `progress` | Handler de Continuar passou; prateleiras R2 corrigidas |
| Filmes / Series / Animes / Doramas | L/R, destaque, cards, retorno e falha de carregamento | `tab-home`, `anime-home` | Teste de tab-home passou; contrato estatico OK |
| Sagas / detalhe de saga | Navegacao, variantes, A, B, toque em card | `sagas`, `sagas/:slug` | Teste de seed passou; contrato estatico OK |
| Busca | Digitar, filtros, card, B, toque e rolagem | `search-v2` | Teste de busca passou; contrato estatico OK |
| Filme / relacionados | Assistir, favorito, lista, relacionado, B | `movie/:id/info` | Contrato estatico OK; hardware pendente |
| Serie / episodios | Temporadas, sinopse, audio, Assistir, B, download em lote | `series/:id`, `accel/download-batch` | Contrato estatico OK; erro HTTP do lote corrigido |
| Historico / Continuar | Retomar, recomecar, concluir, remover, listas, biblioteca, toque | `sync/progress`, `watchlater`, `accel/jobs` | Teste de proximo episodio passou; toque corrigido |
| Configuracoes / preferencias / diagnostico | Navegacao, troca de perfil, versao, update, B | `account/me`, `prefs`, `accel/status` | Contrato estatico OK; hardware pendente |
| Player | Abrir, retomar, cancelar, buscar, faixas, recuperar, fechar | `stream`, `variants`, `refresh`, `fail`, `heartbeat`, `stop` | Build e prova de pacote R2 passaram; console pendente |

`tools/audit_switch_routes.mjs` validou 9 telas e 36 combinacoes de metodo,
caminho e registro do backend. Isso detecta rota ausente ou prefixo errado,
mas nao prova rede, controle, grafico ou decodificacao no console.

## Falhas confirmadas e corrigidas

1. A pipoca do player era desenhada so uma vez antes de chamadas sincronas de
   `avformat_open_input`, sondagem, seek e preroll. O callback de interrupcao
   agora redesenha a tela a cada ~80 ms somente na thread do renderer; o loop
   de preroll tambem bombeia a animacao. O atlas permanece ate o primeiro
   quadro em vez de ser liberado ao entrar no loop de leitura. B continua
   cancelando a tentativa. MP4 remoto agora tambem possui limite de 45 s ate
   o primeiro quadro.
2. A Home lia `movieShelves`/`liveShelves`, mas a API atual devolve
   `readyMovieShelves`/`readySeriesShelves`. Ambas as prateleiras R2 aparecem
   agora, tipadas corretamente como filmes e series. `movieShelves` antigo
   continua como fallback.
3. Na aba Historico, qualquer toque no corpo da tela chamava A sobre a
   selecao anterior, podendo abrir a obra errada. O hit-test agora usa as
   coordenadas dos cards, listas e episodios preparados. Toques vazios nao
   executam. Perfis e acoes de Configuracoes ganharam hit-tests equivalentes.
4. O menu de preparacao em lote de episodios anunciava sucesso mesmo quando
   `/api/accel/download-batch` devolvia erro, inclusive 503. Agora mantem a
   selecao e mostra a falha.

## Provas de dados e playback

- `catalog-continue-next-ep-test.mjs`: passou com `R2_ENABLED=0` e DB temporario.
- `catalog-tab-home-test.mjs`, `catalog-search-test.mjs`,
  `read-model-swr-test.mjs`, `saga-seed-test.mjs` e
  `playback-priority-guard-test.mjs`: passaram no checkout local.
- Item R2 12554: manifesto e sondagem ffprobe para retomada em 48 s e
  600 s passaram; foram localizados os pacotes de video antes e depois da
  posicao salva. FFmpeg decodificou 3 s no inicio e nos dois pontos de
  retomada. Essa prova usa FFmpeg do host, nao o decoder do Switch.
- `tools/validate_release.ps1`: compilacao ARM64 limpa, simbolos, TLS,
  certificado e contrato basico do site; `git diff --check` sem erros.
- `npm run check` no backend local passou. `npm test` completo parou em um
  conflito preexistente fora do Switch: `nightly-schedule-test.mjs` espera
  `Tue,Fri`, mas o timer versionado executa diariamente `04:15`. O mesmo
  descompasso existe em `origin/main`. As provas isoladas acima passaram.

## Riscos abertos

- Sem console ou emulador funcional neste host, nao ha validacao fisica de
  FPS, toque, audio, memoria e estabilidade da retomada no NRO. O teste R2
  acima confirma os bytes e o seek do pacote, nao a apresentacao no console.
- `/api/stream/session/:id/fail` ainda desativa uma fonte globalmente apos a
  falha de um cliente. `/api/play/:id` ainda pode resolver outro formato por
  tras de um `source_id` assinado. Ambos exigem mudanca coordenada no backend
  e nos outros clientes; ver `PLAYBACK_ROUTE_AUDIT_2026_09_24.md`.
- A rota de aceleracao local pode devolver 503 quando o servidor opera em
  modo somente R2. O Switch agora informa a falha do lote, mas itens que so
  tenham torrent dependem de uma fonte R2 ou de reabilitacao explicita do
  acelerador; o cliente nao pode inventar bytes de video.
