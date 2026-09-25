# Nplay Switch 0.12.12

Corrige a pipoca congelada durante a abertura e retomada do video: ela agora
continua animada enquanto FFmpeg le manifesto, faixas, segmentos e o ponto
salvo. A abertura remota de MP4 termina com erro controlado se nenhum quadro
chegar em 45 segundos. A Home exibe as prateleiras R2 atuais de filmes e
series. O toque no Historico abre apenas o card escolhido. A preparacao em
lote de episodios nao anuncia sucesso em erro HTTP.

Validacao: 9 telas e 36 contratos HTTP conferidos contra `origin/main`;
compilacao ARM64 limpa; testes isolados de catalogo, busca, Continuar, sagas
e cache aprovados; manifesto R2 e decodificacao host aprovados para retomada
em 48 s e 600 s. O NRO nao foi executado em um Switch neste ambiente. A
matriz completa esta em `docs/SWITCH_ROUTE_AUDIT_2026_09_25.md`.
