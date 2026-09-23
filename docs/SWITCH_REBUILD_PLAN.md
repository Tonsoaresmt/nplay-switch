# Nplay Switch 0.11: reconstrução incremental

O Switch continua um cliente nativo SDL2/FFmpeg/libcurl. A TV web é a referência
de organização visual e de recursos, não um runtime a ser embutido no NRO. O
backend e a conta permanecem compartilhados; o NRO mantém o player, os downloads
offline e os controles próprios do console.

## Linha de base

- Release pública: v0.10.1, com `Nplay.nro` no GitHub Releases.
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
3. **Paridade de conta e catálogo.** Perfil ativo por dispositivo e header
   `X-Profile-Id` nas chamadas autenticadas; conexão por código/QR, sagas,
   favoritos, Minha lista, histórico e pedidos preparados. Migrar os dados
   locais atuais sem apagar token, preferências ou downloads offline.
4. **Reprodução.** Matriz de fontes por tipo de obra: R2 HLS/MP4, origem direta
   compatível e preparação já gerenciada para torrent. Tratar embed como
   indisponível no Switch sem iniciar um player vazio. Validar áudio, legenda,
   seek, retomada, expiração da URL, failover, suspensão e retorno. O Fastify
   não passa a retransmitir vídeo.
5. **Distribuição.** Versão nova exige GitHub Release com `Nplay.nro`; push de
   código sozinho não atualiza o console. Verificar tamanho, SHA-256 e cabeçalho
   antes da troca, preservar caminho instalado e testar atualização 0.10.1 →
   0.11.x no Switch real antes de publicar como `latest`.

## Critérios antes da release pública

- Build ARM64 limpo e `tools/validate_release.ps1` aprovados.
- Navegação portátil/dock, troca rápida de abas, busca e detalhes com rede lenta,
  falha e cancelamento sem congelar a interface.
- Filme e episódio HLS/R2 por pelo menos 30 minutos, com áudio/legenda, seek e
  retomada. Registrar heap e trace do player; não usar só o build como prova.
- Atualização pelo menu do Nplay, reinício e conferência da versão em uso, sem
  copiar o NRO manualmente, partindo da versão instalada pelo proprietário.
