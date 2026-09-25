# Nplay Switch 0.12.15

A abertura de fontes torrent no Switch agora consulta a rota TorBox/hot-stream
usada pelo navegador. O NRO espera o buffer inicial com animação e cancelamento,
abre o link direto quando pronto e volta à resolução R2 quando o preparo já foi
concluído. O endpoint escolhe a fonte torrent validada; o ID retornado pela rota
geral pode não satisfazer o filtro de confiança do hot-stream. O acelerador
antigo permanece como fallback apenas em servidores sem essa rota.

A primeira resolução de fonte não bloqueia mais o desenho da tela de abertura.
A leitura do ponto salvo tem limite de cinco segundos. Fontes remuxadas em tempo
real, sem Range, desativam seek e explicam quando uma retomada ainda depende da
publicação R2. As fontes R2 e MP4 com seek mantêm a retomada normal. O HUD do
player ganhou barras translúcidas, linha de tempo mais discreta e controles
contextuais, preservando os atalhos do Switch.

A barra de navegação e os filtros de busca agora exibem apenas Início, Filmes,
Séries, Animes, Sagas e Histórico. O detalhe de filmes elimina marcadores de
fonte/qualidade do rótulo de gênero. O ícone JPEG incorporado ao NRO ocupa toda
a área quadrada. O atalho HOME instalado tem ícone próprio no forwarder NSP;
somente atualizar o NRO não altera esse ícone.

Validação: compilação ARM64 limpa, contratos de 37 rotas, simulações host de
TorBox/R2 e continuação de episódios, remux MKV→fMP4 chunked com áudio/vídeo,
conferência do JPEG dentro do NRO e `git diff --check`. Sondas somente leitura
de R2 mostraram taxa suficiente em amostras, mas uma leitura de 30 segundos
expirou e um segmento de episódio oscilou entre timeout e entrega rápida.
Esses resultados não certificam fluidez no Switch real e não permitem afirmar
que o stutter R2 está eliminado.
