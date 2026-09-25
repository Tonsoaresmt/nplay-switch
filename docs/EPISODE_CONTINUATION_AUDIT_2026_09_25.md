# Continuidade de episodios no Switch — 25/09/2026

## Causa confirmada

- O auto-avanco so existia no `input_series(JOY_A)`. Continuar da Home e do
  Historico chamavam `resolve_and_play` diretamente; ao EOF a tela de origem
  permanecia sendo a Home. O usuario tinha de localizar a serie outra vez.
- O loop do detalhe parava no fim de `ser_nep()`, que representa somente a
  temporada visivel quando a serie nao e agrupada.
- A Biblioteca usava a ordem de `listJobs()` (estado e data), nao a ordem T/E,
  e saltava imediatamente para o proximo indice sem contagem ou verificacao
  de lacunas.
- O cliente rejeitava `duration_seconds=0` antes de enviar progresso, embora
  o servidor o aceite e o site salve a posicao em HLS sem duracao conhecida.

## Correcoes

- Entrada por detalhe, Home/Continuar e Historico compartilha a sequencia de
  episodios. Depois de EOF natural, o detalhe da serie fica aberto. O proximo
  episodio e escolhido por temporada/numero, inclusive atravessando a proxima
  temporada e a proxima serie de um grupo editorial. Quando o detalhe ainda
  nao esta carregado, a consulta usa `CatalogFetch` cancelavel; nao bloqueia
  a interface durante a rede.
- A preferencia `autoplayNext` governa a contagem de 5 s. Com ela desligada,
  o proximo episodio apenas fica selecionado no detalhe. B cancela a contagem
  e tambem deixa o detalhe aberto. Toque funciona nos botoes de continuar,
  recomecar, assistir o proximo e cancelar.
- A Biblioteca ordena jobs por T/E. Avanca somente se o proximo estiver pronto
  e for contiguo; oferece a mesma contagem. Lacunas e coordenadas ausentes
  encerram a sequencia sem reproduzir um episodio errado.
- A consulta de marcas de visto da Biblioteca deixou de executar na thread
  SDL. Uma resposta antiga e descartada quando o usuario troca de obra ou
  perfil; o detalhe continua navegavel enquanto a rede responde.
- Progresso com duracao zero e enviado ao backend. Quando a sincronizacao de
  EOF confirma conclusao, a marca do episodio no detalhe e na Biblioteca e
  atualizada localmente.
- O detalhe mostra a chave real da temporada (inclusive Especiais), pois o
  indice visual podia divergir do numero recebido do catalogo apos uma lacuna.

## Verificacao e limites

- `tools/test_episode_flow.c` exercita temporada atual, virada de temporada,
  serie agrupada, fim, item ausente e lacuna da Biblioteca.
- `tools/validate_release.ps1` cobre build ARM64 limpo, TLS, simbolos do
  player e contrato do site. `tools/audit_switch_routes.mjs` confere as
  chamadas do cliente com as rotas do backend `origin/main`.
- O backend ja entrega `series_id` no Continuar da Home e no Historico.
  Nenhum endpoint novo ou alteracao de banco foi necessario.
- Um episodio cuja unica fonte seja embed continua indisponivel no NRO. A
  transicao mostra a falha da fonte, mas nao troca para outra obra. O arquivo
  compilado e as provas do host nao substituem reproducao em hardware real.
- O desktop tambem oferece anterior/proximo dentro do HUD do player. O NRO
  ainda exige sair do video para escolher manualmente outro episodio; isso
  continua como diferenca de interface, sem afetar o auto-avanco ao EOF.

## Revisao adicional da 0.12.14

O teste anterior de `episode_flow.c` verificava a ordem, mas nao executava o
aplicativo completo. A revisao de chamadas e estado encontrou tres conflitos:

- HLS com duracao desconhecida enviava a posicao, porem o backend so considera
  `completed=1` quando recebe duracao positiva. Apos EOF com quadro exibido, o
  NRO agora usa `/api/sync/item-watched` (limite de 6 s) se o ultimo progresso
  nao confirmou conclusao. Saida com B e erro nao marcam como visto.
- Um EOF sem primeiro quadro podia ser interpretado como episodio concluido e
  saltar a obra. O player agora encerra essa condicao como erro e conserva o
  progresso anterior.
- Um novo fetch de serie ou um fetch que nao iniciou mais nao deixa autoavanco
  pendente. O polling da Biblioteca preserva o grupo e o episodio selecionado
  por IDs quando a ordem dos jobs muda; se a obra sumir, retorna a Biblioteca.

| Area | Prova local | Resultado |
| --- | --- | --- |
| Episodios, temporadas, grupo, lacunas e reordem dos jobs | `tools/test_episode_flow.c` compilado no host | Passou |
| 9 telas e 37 contratos HTTP Switch/backend `origin/main` | `tools/audit_switch_routes.mjs` | Passou (contrato estatico) |
| Player, TLS, HLS, simbolos, NRO ARM64 | `tools/validate_release.ps1` com build limpo | Passou |
| Continuar da serie e contrato integrado de playback | `catalog-continue-next-ep-test.mjs` e `player-flow-contract-test.mjs`, banco temporario | Passou |
| Sintaxe e checks do backend | `npm run check` | Passou |
| Suite geral do backend | `R2_ENABLED=0 npm test` | Parou em `nightly-schedule-test.mjs`: espera Ter/Sex, mas timer real e diario; falha preexistente fora do Switch |
| Preferencias por dispositivo | `npm run test:device-preferences` | Passou |
| Shell da TV e imagens Android | `npm run test:tv-shell` e `android-image-contract-test.mjs` | Passaram |
| Navegacao/compatibilidade TV legada | `npm run test:tv-navigation` e `player-compat-test.mjs` | Testes antigos falharam: nome de import com cache-bust desatualizado e fixture sem `_isTv`; nao medem o NRO |
| Reproducao, seek, audio, touch e stutter no console | NRO em hardware | Nao executado nesta rodada; nao ha Switch ou emulador disponivel neste ambiente |

Esses testes comprovam a compilacao, contratos e estados exercitados no host.
Nao demonstram que todos os caminhos visuais, rede R2 e decoder funcionam no
Switch real. A release continua sujeita a essa verificacao fisica.
