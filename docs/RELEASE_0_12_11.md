# Nplay Switch 0.12.11

Corrige conflitos entre o Historico, a escolha de episodios e a recuperacao de
fonte. O detalhe de serie agora seleciona o episodio parcialmente assistido
mais recente. Uma tentativa sem quadro nao atualiza o progresso salvo. O
fallback nao desativa a unica fonte por engano nem reaproveita o formato da
fonte antiga quando a API aponta para outra.

Validacao local: compilacao ARM64 limpa, contrato do NRO/site e verificacao de
diff. O NRO nao foi executado em hardware Switch neste ambiente.

O risco estrutural do endpoint `/fail` no servidor esta registrado em
`docs/PLAYBACK_ROUTE_AUDIT_2026_09_24.md` para uma correcao coordenada com os
outros clientes.
