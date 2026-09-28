# Nplay Switch 0.12.18

O player agora normaliza os idiomas informados pelas fontes (por exemplo `por`,
`pt-BR`, `eng`, `jpn` e nomes escritos no titulo da faixa) antes de escolher
audio e legenda. A preferencia da conta passa a ser carregada depois do primeiro
catalogo, sem disputar rede com a abertura do aplicativo. Quando os metadados de
idioma nao existem, episodios seguintes preservam o indice de audio usado no
episodio anterior.

Faixas HLS de audio, legenda, dados e anexos que nao estao em uso ficam
descartadas, evitando baixar varias playlists em paralelo. Ao trocar uma faixa,
o player ativa somente a nova e ignora amostras antigas anteriores ao ponto
atual. A espera entre quadros foi dividida em intervalos curtos para que comandos
do controle sejam percebidos sem a antiga demora de ate 350 ms.

Series com varias versoes de audio agora percorrem todas elas com ZL/ZR. Ao
trocar uma temporada agrupada com L/R, o aplicativo tenta manter a mesma versao
e avisa quando ela nao existe. Recuperacoes de sessao mantem a faixa escolhida e
o pool HLS e liberado completamente ao sair do player.

Validacao local: compilacao devkitA64 sem erros nem avisos, verificacao completa
de release e simulacoes de contrato. Ainda e necessario confirmar em hardware
filme, serie, anime e dorama, troca de faixa durante HLS e continuidade entre
episodios, pois a compilacao nao substitui o teste no Nintendo Switch real.
