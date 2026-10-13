# Nplay Switch 0.12.53 — compatibilidade com o site

- TorBox MP4: respeita Range, permite retomada/busca pelo fluxo existente e
  renova a mesma fonte com checkpoint preservado; R2-ready exige descriptor R2.
- Pedidos de Play: confirma o inicio em segundo plano somente depois de video
  apresentado, com cancelamento e tentativas limitadas. Nao cria central visual
  completa de pedidos nem altera regras de planos/promocao.
- Animes: conserva contexto da obra/episodio ao entrar no TorBox; novas rails
  Novas temporadas e Mais bem avaliados; Populares passa a Em alta.
- Visual acompanha o site sem selo Pronto nos posters/destaque. Prioridade R2
  mantida. Respostas HLS TorBox sao reconhecidas, sem forcar geracao na VPS.
- TLS corrigido e protecao contra falsa reconexao de buffer cheio preservados.

## Validacao e limites

Build ARM64 -Werror e bateria completa sem SkipMediaFixtures passaram no
artefato final, incluindo midia/simbolos, playback, legendas e contrato do
commit implantado c9d0f77. Testes de TorBox usam providers/transporte simulados
ou servidor local: NAO comprovam playback real no console.

MKV TorBox com seek_mode=restart ainda exige integracao completa de keyframe,
clocks e legendas; nao anunciar seek/retomada/recovery desse caminho como pronto.
HLS EVENT TorBox nao e equivalente a VOD R2 imutavel; nao ativado automaticamente.
Teste fisico da .53 segue pendente. Publicacao autorizada para teste no Switch.

## Instalacao

Configuracoes > Buscar atualizacao. Se a versao instalada tiver erro HTTPS que
impeca baixar, substitua apenas Nplay.nro na pasta switch da microSD. Preserve
configuracoes e demais arquivos. Reabra o aplicativo apos atualizar.

Nplay.nro: 24270671 bytes.
SHA-256: 8d877c7463f58649454353899535b94acb5e0afc087bdf136684019614cba5b7

Relatorio: docs/SITE_COMPATIBILITY_0_12_53.md.
