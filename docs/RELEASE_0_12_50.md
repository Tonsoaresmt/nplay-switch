# Nplay Switch 0.12.50

Atualização publicada a pedido do usuário para teste no Switch pelo atualizador.
**Compilação e validação completa local aprovadas; teste em Switch físico ainda
pendente.** Esta release não garante ausência de travamentos em toda fonte/rede.

## Navegação, visual e desempenho

- Direcional reconhece toques rápidos sem duplicar o movimento. Repetição não
  transporta automaticamente a direção para outra tela, aba ou menu de perfil.
- Botão Buscar com desenho e área de toque alinhados; barra indica a pesquisa ativa.
- Cards com foco mais limpo e títulos longos com reticências sem quebrar acentos.
- Busca guarda somente a janela visível, sem eliminar resultados nem alterar filtros.
- Capas com recuperação de falhas, URLs longas preservadas, filas e cache limitados
  por quantidade e memória. Fotos circulares dos perfis e pipoca preservadas.

## Player, legendas e continuidade

- Pausa preservada ao reabrir a mesma reprodução após seek, troca de faixa ou recuperação.
- Fim antecipado com erro não é tratado como episódio concluído; decoders são drenados.
- Legenda externa incompleta não substitui a faixa válida; escolha exata e desligamento
  manual são preservados. Substituição/cancelamento R2 não faz join bloqueante na UI.
- Histórico acompanha a identidade da obra, mantém seleção em reordenação e protege
  dados conhecidos quando a resposta é inválida.
- Contexto de foco/scroll por aba; touch nos painéis Episódios e timeline. Arrastar
  a timeline apenas pré-visualiza: é necessário confirmar para executar o salto.
- Assistir mais tarde reconcilia exclusões confirmadas sem apagar entradas locais incertas.

## Validação e limites

Build ARM64 completo sem avisos (`-Werror`), bateria local completa, fixtures reais
HLS/VTT/remux, regressões de pausa/seek/legendas/episódios e testes de navegação/capas
aprovados. Busca exercitada com 5.000 resultados e quatro filtros; cache com 3.000
entradas e falhas de rede/alocação simuladas.

Ainda pendentes: visual/GPU/NVTEGRA no console, concorrência real dos workers de capas,
Wi-Fi e sessão longa. Fontes hot/progressivas ainda têm alguns waits no encerramento.
Nenhum backend ou pacote R2 foi reprocessado nesta publicação. Atualizar o NRO não
cria legendas ausentes no pacote nem comprova a causa dos incidentes antigos.

## Instalação

No Nplay, abra **Configurações → Buscar atualização** e instale a versão 0.12.50.
Após reiniciar, confira a versão em Configurações.

Alternativamente, baixe **Nplay.nro** abaixo, conecte o microSD do Switch ao computador
ou celular e substitua o arquivo Nplay.nro existente dentro da pasta **switch**
(ou sua subpasta de instalação). Reabra o aplicativo.

Nplay.nro: **24.262.479 bytes**.

SHA-256: `bfa8e19e8592c6d0616b98e8e6e09b2e0c5a453b66f13598824dd3e8527cc96a`.

[Relatório de navegação e testes](https://github.com/Tonsoaresmt/nplay-switch/blob/v0.12.50/docs/APP_NAVIGATION_VISUAL_PERFORMANCE_0_12_50.md)
e [relatório dos fluxos do player](https://github.com/Tonsoaresmt/nplay-switch/blob/v0.12.50/docs/PLAYER_FLOW_FIXES_0_12_50.md).
