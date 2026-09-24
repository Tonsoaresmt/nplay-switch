# Nplay Switch 0.12.3

Esta versão ajusta a interface do NRO a partir dos problemas fotografados no
console. Os pôsteres da Home, busca, Histórico, Biblioteca e relacionados passam
a manter a proporção original; o destaque separa texto e arte para que a imagem
não fique cortada. Os cards principais ganharam altura adequada a pôsteres.

O detalhe de série agora usa a área principal para o episódio selecionado:
miniatura, título e sinopse em linhas legíveis, com rolagem por cima/baixo.
A aba Sagas consulta a curadoria já publicada em `/api/catalog/sagas`, permite
trocar variantes e abre as obras na ordem fornecida pela API. Obras marcadas
como indisponíveis não iniciam reprodução.

No modo portátil, toques abrem abas, obras, filtros e episódios. Deslizes
percorrem Home, fileiras, Sagas, busca e sinopses. D-pad e botões permanecem
disponíveis. O transporte de vídeo da 0.12.2 foi preservado.

Validação local: contrato site/Switch, compilação ARM64 limpa,
`tools/validate_release.ps1` e `git diff --check`. A fluidez de HLS/R2 e o
toque ainda precisam ser confirmados no console real; o roteiro está em
`docs/SWITCH_0_12_HARDWARE_CHECK.md`.

O ícone embutido no NRO já usa a marca nova. O ícone do atalho HOME pertence ao
forwarder NSP instalado; atualizar o NRO não altera esse NSP. Ele exige uma
substituição separada do forwarder.
