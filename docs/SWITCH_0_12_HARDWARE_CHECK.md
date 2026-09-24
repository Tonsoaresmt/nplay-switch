# Nplay Switch 0.12.2: teste de reprodução no console

O teste da 0.12.1 reproduziu pausas a cada poucos segundos em vários títulos,
apesar de o diagnóstico mostrar `buffer 0`: esse contador não media o tempo
bloqueado dentro de `av_read_frame`. A 0.12.2 reutiliza conexões HLS ociosas e
mostra a duração dessas esperas. Atualize pelo menu do Nplay e confirme a versão.

1. Em Configurações, confirme **Versão 0.12.2**. Compare a Home com a TV:
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
4. Na Home e nas abas Filmes/Séries, confira se um destaque de filme abre
   detalhe de filme e se um destaque de série abre detalhe de série. A vitrine
   das abas deve usar os destaques editoriais prontos que aparecem no site.
5. Reproduza por pelo menos 10 minutos um filme e um episódio R2 que funcionem
   no site. Conte pausas perceptíveis e observe se o vídeo pula trechos após
   uma pausa. Teste também Continuar assistindo, pausa, busca para o meio do
   vídeo, áudio/legenda e retorno ao catálogo.
   Depois de sair, em Configurações > X, anote `descartados`, `HW`, `Pausas`,
   `leituras lentas` e `max ms`. `Pausas` conta intervalos de pelo menos 250 ms
   entre quadros; `leituras lentas` separa espera de HLS de queda do decoder.
6. Se surgir o HTTP
   404, abra **Configurações > X** e anote a linha de rede com método e caminho
   (GET ou POST, `/api/...`) e o ID do título/episódio. Não copie token, senha
   ou URL assinada. Essa linha distingue catálogo, autorização e mídia.
7. Depois de uma pausa, copie as últimas linhas de
   `sdmc:/switch/.nplay-player-trace.log`. Eventos `avio close` anormais ou
   amostrados registram requisições, maior tempo até o primeiro byte e
   esvaziamentos do buffer;
   `demux buffering-end` mede a duração da pausa. Esses dados separam lentidão
   de rede de quadros perdidos no decodificador.

O build local e os testes de contrato não comprovam fluidez no hardware.

O ícone do atalho no menu HOME pertence ao forwarder NSP, não ao NRO. O NRO
contém o ícone novo para o hbmenu e agora também o usa no cabeçalho interno.
