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
