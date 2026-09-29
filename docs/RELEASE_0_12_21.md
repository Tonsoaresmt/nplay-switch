# Nplay Switch 0.12.21

## Correcões desta rodada

- A tela de preparacao volta a usar a pipoca animada embutida no NRO. O anel
  pequeno permanece apenas como indicador secundario de atividade.
- Ao trocar o audio de um HLS, a nova rendition e ativada e o demuxer e
  reposicionado uma unica vez no ponto atual. Isso evita que a faixa tente ler
  segmentos antigos ate alcancar o video e deixa a operacao cancelavel por `B`.
- A troca de audio e a ativacao de legenda agora exibem uma espera animada e
  registram inicio, seek e conclusao no diagnostico persistente.
- Renditions de legenda do R2 que ja existem no master HLS, mas ficaram com
  codec `NONE` por causa da sondagem curta, sao reconhecidas pelo contrato do
  empacotador como WebVTT. As opcoes deixam de desaparecer do painel.
- Legendas repetidas no mesmo idioma recebem posicao (`1/8`, `2/8` etc.),
  permitindo distinguir todas as faixas publicadas pelo servidor.
- Episodios abertos diretamente pela Home ou pelo Historico levam o objeto do
  catalogo ao player. O HUD passa a mostrar serie, temporada/episodio, titulo e
  sinopse quando esses campos estiverem disponiveis. A resposta de `/stream`
  fornece um fallback de temporada/episodio quando o detalhe nao foi carregado.
- O contexto global de uma serie so e reutilizado quando o `series_id` confere;
  isso impede metadados e preferencia de audio de outra serie aberta antes.

## Verificacao local

- Suite limpa completa concluida sem erros nem avisos.
- `tools/validate_release.ps1` cobre pipoca, realinhamento de audio HLS,
  recuperacao das faixas WebVTT e fallback de metadados do episodio.
- Artefato: `Nplay.nro`, 24.143.695 bytes,
  SHA-256 `8683e64099e8ba3ec834a5990c0e7987481e8a0e91a54c8ac5a05b7b68058930`.
- A confirmacao de tempo real ainda exige o Switch: trocar audio no meio de um
  filme/episodio R2 e abrir o painel de legendas de um titulo que possua varias
  faixas, como o exemplo enviado pelo usuario.
