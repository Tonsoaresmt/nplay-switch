# Nplay Switch 0.12.13

O proximo episodio agora e oferecido ao terminar um episodio iniciado pela
Home/Continuar, pelo Historico ou pelo detalhe da serie. A contagem de 5 s
respeita a preferencia de reproducao automatica; cancelar deixa a serie aberta
com o proximo episodio selecionado. A sequencia atravessa temporadas da mesma
serie e temporadas agrupadas sem voltar para a Home.

A Biblioteca ordena episodios preparados por temporada/episodio e so avanca
automaticamente para um vizinho contiguo que esteja pronto. Os dialogos de
retomada e proximo episodio aceitam toque. Progresso HLS com duracao ainda
desconhecida agora e sincronizado com a conta, como no site. Temporadas com
numeracao descontigua mostram o numero real no detalhe. A consulta das marcas
de visto da Biblioteca ocorre em segundo plano, sem travar a navegacao.

Validacao: teste local da ordem de episodios e lacunas; build ARM64 e contrato
do site. O NRO nao foi executado em um Switch neste ambiente. Detalhes em
`docs/EPISODE_CONTINUATION_AUDIT_2026_09_25.md`.
