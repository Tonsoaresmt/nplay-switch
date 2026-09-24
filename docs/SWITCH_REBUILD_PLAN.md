# Nplay Switch: reconstrução nativa guiada pelo modo TV

## Correção de escopo em 0.12.0

A 0.11.0 publicada estabilizou consultas e perfis, mas conservou o layout SDL
antigo. Ela não foi uma reformulação visual. A 0.12.0 em desenvolvimento muda
de fato o cabeçalho, a logo em tempo de execução, o foco branco, as cores, o
destaque horizontal, as fileiras de cinco cards, a busca de cinco colunas, os
detalhes de filme e os episódios de série em cards com imagem. O D-pad navega
esses episódios horizontalmente; L/R continuam trocando temporadas.

Esta versão ainda não é paridade funcional completa com a TV web: Sagas,
Favoritos, Minha lista e Pedidos permanecem sob fluxos nativos existentes ou
sem aba dedicada. A falha HTTP 404 relatada pelo usuário ao iniciar reprodução
na 0.11.0 também precisa de diagnóstico no console; o trabalho visual não a
resolve. Não publicar como `latest` antes de validar ambos no hardware.

O Switch continua um cliente nativo SDL2/FFmpeg/libcurl. A TV web é a referência
de organização visual e de recursos, não um runtime a ser embutido no NRO. O
backend e a conta permanecem compartilhados; o NRO mantém o player, os downloads
offline e os controles próprios do console.

## Linha de base

- Base da atualização automática: v0.10.1, com `Nplay.nro` no GitHub Releases.
- O catálogo usa cinco landings, cache de até 160 texturas e três workers de
  capas. A abertura do player libera landings e capas por causa da pressão de
  memória observada no hardware. Não reintroduzir prefetch das cinco abas.
- O player nativo suporta HLS/MP4, faixas, progresso, refresh e failover de
  sessão. `embed` de navegador não é uma fonte reproduzível neste cliente.
- O atualizador instalado precisa ser confirmado no console: versões anteriores
  à correção do caminho de instalação podem requerer um último bootstrap manual.

## Etapas

1. **Fundação 0.11.0 em andamento.** Rede e parse de busca/detalhes fora da
   thread SDL, uma requisição ativa e no máximo uma intenção pendente. Cancelar
   com B deixa a interface imediatamente. Validar estados vazios, falha e troca
   rápida entre telas. Medir no Switch tempo de resposta dos botões, frames
   longos e heap antes/depois de abrir o player.
2. **Shell nativo inspirado na TV.** Refazer topo, destaque, fileiras, foco e
   detalhes com componentes SDL reutilizáveis. Manter densidades adequadas a
   portátil (1280×720) e dock; carregar/desenhar só itens visíveis. Preservar
   navegação A/B, D-pad, analógico, L/R e acessibilidade de foco.

### Primeiro passo do shell na branch de desenvolvimento

- A Home guarda a quantidade de itens por fileira ao receber o catálogo. A cada
  quadro desenha apenas o destaque e as fileiras dentro da tela, percorrendo
  diretamente os cards horizontais visíveis. Navegação, posição dos cards e
  comportamento de busca permanecem iguais.
- Configurações > X mostra contagem de quadros de interface acima de 20/33 ms e
  o pior tempo da sessão. A medição cobre atualização e desenho até o Present,
  mas exclui a chamada bloqueante do player. Não grava esses números na microSD.
- A seleção das janelas horizontais foi comparada com o desenho anterior em
  6.714 combinações de tamanho e foco; sem diferenças de cards visíveis. Falta
  medir o ganho e testar rolagem longa em hardware portátil e dock.
- A busca mantém apenas cinco contagens escalares por resultado recebido, em
  vez de recontar todos os filtros a cada quadro. O desenho percorre as duas
  listas de resultados uma vez por quadro e ignora cards fora da tela, sem criar
  outra cópia do JSON. Falta verificar a rolagem e a troca de filtros no console.
- Home e busca recortam o conteúdo entre a barra superior e o rodapé. Os helpers
  de texto agora restauram o recorte anterior, evitando que cards parcialmente
  visíveis invadam a navegação ao rolar. Conferir visualmente no Switch.
- Revisão posterior corrigiu o limite vertical da fileira para incluir o texto
  abaixo da capa; a checagem anterior podia ocultá-lo cedo demais. A consulta
  de favoritos após escolher o perfil usa agora o fetch cancelável em segundo
  plano, evitando até 15 s de bloqueio da UI em falha de rede. Enquanto ela
  termina, o atalho de favoritar informa que está sincronizando.
- Roteiro de confirmação no hardware: `docs/SWITCH_0_11_HARDWARE_CHECK.md`.
3. **Paridade de conta e catálogo.** Perfil ativo por dispositivo e header
   `X-Profile-Id` nas chamadas autenticadas; conexão por código/QR, sagas,
   favoritos, Minha lista, histórico e pedidos preparados. Migrar os dados
   locais atuais sem apagar token, preferências ou downloads offline.

### Marco de perfis na branch de desenvolvimento

- O seletor nativo usa `/api/account/profiles`; login novo exige escolher um
  perfil. No boot, o ID salvo é validado contra a lista da conta antes de
  carregar dados pessoais. O ID fica na microSD e as chamadas autenticadas da API
  carregam `X-Profile-Id`. Requisições externas, capas e GitHub não recebem o
  header.
- A troca de perfil na sessão salva o ID e reinicia o app, descartando consultas
  e caches em voo antes de usar a nova identidade. Se o carregador não oferece
  reinício automático, o app encerra após avisar para abrir novamente.
- As listas pessoais locais são separadas por ID. A lista anterior a perfis é
  atribuída ao primeiro perfil escolhido pela conta que a possuía; as demais
  começam vazias. O nome da conta anterior é preservado antes de novo login.
  Token, downloads offline e preferências do player continuam preservados.
- Falta verificar no Switch real: primeiro login, retorno com token salvo,
  troca entre dois perfis, persistência após reinício, falha de rede no seletor
  e encerramento sem suporte a `envSetNextLoad`.
4. **Reprodução.** Matriz de fontes por tipo de obra: R2 HLS/MP4, origem direta
   compatível e preparação já gerenciada para torrent. Tratar embed como
   indisponível no Switch sem iniciar um player vazio. Validar áudio, legenda,
   seek, retomada, expiração da URL, failover, suspensão e retorno. O Fastify
   não passa a retransmitir vídeo.
5. **Distribuição.** A 0.11.0 deve ser publicada como GitHub Release com
   `Nplay.nro`; push de código sozinho não atualiza o console. Verificar tamanho,
   SHA-256 e cabeçalho antes da troca e preservar o caminho instalado. A release
   0.10.1 contém o atualizador que consulta `/releases/latest`, baixa o `.nro`
   e procura cópias Nplay/Meruem no SD. A transição precisa de confirmação no
   Switch real; versões anteriores ao conserto do instalador podem requerer
   bootstrap manual.

## Critérios antes da release pública

- Build ARM64 limpo e `tools/validate_release.ps1` aprovados.
- Navegação portátil/dock, troca rápida de abas, busca e detalhes com rede lenta,
  falha e cancelamento sem congelar a interface.
- Filme e episódio HLS/R2 por pelo menos 30 minutos, com áudio/legenda, seek e
  retomada. Registrar heap e trace do player; não usar só o build como prova.
- Atualização pelo menu do Nplay, reinício e conferência da versão em uso, sem
  copiar o NRO manualmente, partindo da versão instalada pelo proprietário.
