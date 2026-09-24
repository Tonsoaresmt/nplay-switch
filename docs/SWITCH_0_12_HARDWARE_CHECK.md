# Nplay Switch 0.12.0: teste visual e reprodução no console

Este é um candidato da branch `codex/switch-rebuild`, ainda fora da release
`latest`. A 0.11.0 instalada não baixa esta versão pelo menu de atualização.
Guarde uma cópia do NRO atual e abra o candidato pelo hbmenu em modo aplicativo.

1. Em Configurações, confirme **Versão 0.12.0**. Compare a Home com a TV:
   cabeçalho de 95 px, logo nova, destaque panorâmico, cinco cards por fileira
   e contorno branco no foco. Navegue com D-pad e analógico; segure a direção
   por alguns segundos em uma fileira longa.
2. Abra um filme e uma série. O filme deve ter capa à esquerda e informações
   em painel horizontal. A série deve mostrar chips de temporada e cinco
   episódios com miniaturas. Teste esquerda/direita para mudar episódio e L/R
   para mudar temporada. Confira se o título e a sinopse não são cortados em
   uma obra de nome longo.
3. Busque uma obra, troque os filtros com ZL/ZR, abra e volte com B. Confira o
   Histórico e a Biblioteca, inclusive ao rolar além da primeira linha.
4. Reproduza um filme e um episódio que funcionem no site. Se surgir o HTTP
   404, abra **Configurações > X** e anote a linha de rede com método e caminho
   (GET ou POST, `/api/...`) e o ID do título/episódio. Não copie token, senha
   ou URL assinada. Essa linha distingue catálogo, autorização e mídia.
5. Se a reprodução começar, teste pausa, seek, áudio/legenda, retorno ao
   catálogo e retomada. O build local não comprova essas etapas no hardware.

O ícone do atalho no menu HOME pertence ao forwarder NSP, não ao NRO. O NRO
contém o ícone novo para o hbmenu e agora também o usa no cabeçalho interno.
