# Navegação, visual e desempenho — candidato 0.12.50

Rodada de 05/10/2026, sobre `70613b3`, no checkout
`C:/NplaySwitch/.codex-tmp/switch-access-expiry`, branch
`codex/switch-player-hardening`. Não usar o `main` antigo da raiz como base.

## Implementado

- O direcional processa o evento de pressão, inclusive quando pressionar e soltar
  acontece entre quadros. A consulta do botão segurado não duplica esse passo.
  Repetição: 380 ms inicialmente, depois 55 ms, no máximo uma ação por quadro.
  Trocar tela, aba ou abrir/fechar o menu do perfil exige soltar o direcional/
  centralizar o analógico antes de repetir. Um novo toque físico é aceito.
- Busca e abas usam os mesmos retângulos para desenho e toque. O início visível
  de Buscar em x=1026 deixou de ser ignorado. Buscar indica a tela ativa, sem
  manter uma categoria falsamente destacada enquanto a pesquisa está aberta.
  O avatar circular de 70 px e o destaque de 184 px no seletor foram preservados.
- Foco dos posters do catálogo/Histórico usa contorno azul e acento roxo discreto.
  Título e metadados do catálogo foram separados dentro da altura existente.
  Não foram reduzidas capas nem alterados os índices, áreas de toque dos grids ou
  posição das prateleiras. Pipoca, controles e player não foram substituídos.
- Títulos longos em catálogo, Histórico, Biblioteca e listas recebem reticências
  medidas, sem cortar um caractere UTF-8. Cache de ajuste: 64 entradas fixas,
  aproximadamente 129 KiB. Strings maiores que 1023 bytes conservam o recorte
  anterior; não entram nesse cache. Cabeçalhos reservam o espaço da ação Voltar
  e do nome da seção, e restauram o recorte anterior em vez de desativá-lo.
- Busca guarda somente até 32 referências de resultados na área visível. Não
  duplica JSON, não limita a quantidade total de resultados e conserva filtro,
  ordem e tipo. Reconstrói ao mudar janela/filtro; invalida ANTES de excluir o
  JSON de uma busca substituída. Seleção fora da janela mantém o caminho antigo.

## Capas: falhas e memória

Antes, uma falha terminava em `state=3` sem textura e sem nova tentativa. Também
era possível truncar URLs em 719 bytes, mas procurar pelo endereço completo:
isso criava entradas repetidas e downloads inválidos. Agora:

- URLs são copiadas integralmente, no máximo 2047 bytes mais terminador, com
  orçamento agregado de 3 MiB e até 3000 entradas. Alocação falha não remove a
  entrada antiga. Endereços maiores são recusados, nunca truncados ou registrados.
- Falha transitória tenta novamente em 2, 5, 15 e depois 60 s, só enquanto há
  pedidos pela imagem. 404/410 têm intervalo de 5 minutos. Sucesso zera a falha.
- Pedidos na fila sem uso há mais de 1,5 s são descartados antes do HTTP. Voltar
  à imagem a solicita novamente. Uma requisição já em andamento ainda pode ocupar
  um worker até o prazo original de 15 s; não prometer cancelamento por troca de aba.
  Entrar no player cancela a transferência de capa por sua flag própria.
- Corpo da imagem limitado a 4 MiB no callback, mesmo sem Content-Length ou
  com resposta descomprimida. Outros consumidores de `membuf` mantêm `limit=0`.
- Workers reduzem imagens grandes proporcionalmente, sem ampliar as pequenas:
  landscape até 1280×720, portrait até 768 px de altura, quadradas até 384×384.
  Isso mantém resolução acima do avatar e dos posters atualmente desenhados.
- Fila pronta: até 16 surfaces e 16 MiB. Publicação de surface/índice e suspensão
  possuem propriedade coordenada por mutex; excesso é liberado e tenta depois.
- Texturas: limite de 160 **e** orçamento estimado de pixels de 64 MiB. O LRU faz
  espaço antes de criar a nova textura. Reciclagem dos metadados não destrói uma
  textura que o desenho do quadro atual já pediu emprestada. Upload permanece
  restrito a duas imagens por quadro e à thread principal.

Esses números NÃO representam o consumo total do processo/GPU: não incluem
overhead do driver, textos, player nem o pico de decodificação da imagem original
em SDL_image. A redução acontece depois de decodificar; não é uma proteção
completa contra imagem comprimida com dimensões gigantes. Medir no hardware.
O desenho e os uploads seguem o modelo de renderização da
[documentação SDL2](https://wiki.libsdl.org/SDL2/SDL_RenderPresent); os helpers
respeitam a [semântica do recorte](https://wiki.libsdl.org/SDL2/SDL_RenderSetClipRect).

## Evidência local

`tools/test_app_navigation.mjs` extrai os corpos reais de main/text/net para
executáveis C com `-Wall -Wextra -Werror`. SDL, fonte e HTTP são simulados:

- URL de 1499 bytes, deduplicação, limite agregado e falha de malloc;
- 503, atraso de retry, reset no sucesso, cooldown de 404;
- fila antiga fora da tela, rajada de 30 resultados e 100 backdrops;
- suspensão durante HTTP, retomada, falhas de resize/textura, liberação;
- reciclagem após 3000 entradas sem destruir a textura emprestada no quadro;
- callback real de rede rejeitando bytes adicionais sem depender de cabeçalho;
- toque rápido, repetição sem duplicidade/rajada, neutralização e wrap de ticks;
- geometria compartilhada da barra e alvos separados;
- acentos, japonês e emoji; 10000 acertos no cache sem medir novamente;
- 5000 resultados/4 filtros; ordem e tipos; 240000 chamadas quentes à janela
  sem repetir a filtragem JSON; substituição sem reusar ponteiros antigos.

Não são medições de FPS, internet real, renderização de pixels ou concorrência
real dos três workers de capas. Os testes pthread do player continuam separados.
FFmpeg de fixtures do host e FFmpeg 7.1 do NRO não são equivalentes.

Build ARM64 completo `make -B -j4`, com `-Werror`, passou. Depois das últimas
mudanças de main.c houve rebuild incremental. Duas validações completas passaram,
a segunda no artefato FINAL abaixo, sem `SkipMediaFixtures`. Incluem regressões
de player, pausa/seek, legendas, continuidade de episódios, memória e mídia real
do host. `git diff --check` também passou. Não equivalem a homologação física.

Reproduzir a validação no checkout indicado:

```powershell
$env:HOST_CC='C:\devkitPro\msys2\usr\bin\gcc.exe'
$env:TARGET_NM='C:\devkitPro\devkitA64\bin\aarch64-none-elf-nm.exe'
$env:NPLAY_BACKEND_ROOT='C:\NplaySwitch\.codex-tmp\backend-access-expiry'
node tools/test_app_navigation.mjs
./tools/validate_release.ps1 -SkipBuild
```

O backend nessa variável é somente uma cópia local para contratos; nenhuma conta
ou sessão de produção foi criada pelos testes.

Artefato local: `Nplay.nro`, 24262479 bytes.
SHA-256: `bfa8e19e8592c6d0616b98e8e6e09b2e0c5a453b66f13598824dd3e8527cc96a`.
Versão continua candidata **0.12.50**, sem release/deploy/reprocessamento.

O commit desta rodada não executou: o serviço de aprovação automática atingiu
seu limite de uso antes de iniciar o comando. HEAD permanece `70613b3`; as
alterações estão salvas localmente, sem stage. O arquivo NRO e o hash acima foram
reconferidos. Retomar o commit pelo fluxo normal quando a aprovação estiver
disponível, sem contorná-la. Não existe atualização nova publicada no GitHub.

## Próximos testes no Switch (obrigatórios para homologação física)

1. Toques rápidos no D-pad/analógico e troca por L/R; manter direção ao abrir
   outra tela/menu não deve mover o foco novo sozinho. Soltar deve liberar.
2. Touch em toda a largura de Buscar, em abas e no perfil. Busca → detalhe → B
   conserva consulta, filtro, card e scroll; controle e touch devem alternar.
3. Verificar 1280×720: nome longo/acento/japonês, Voltar longo, posters, Histórico,
   Biblioteca/listas, avatar e pipoca; conferir contraste a distância/overscan.
4. Rolagem longa e rápida, trocar várias abas, perda/retorno de Wi-Fi: capas
   devem voltar, não permanecer vazias e não causar tempestade de requests.
5. Busca grande/filtros no meio/fim; confirmar que nenhuma obra sumiu ou mudou
   de tipo. Substituir a pesquisa enquanto dados anteriores ainda carregam.
6. Reproduzir após navegar por muitos posters, retornar e recarregar as capas;
   observar estatísticas de frame já disponíveis no diagnóstico. Comparar com
   a versão anterior no mesmo aparelho/rede; não afirmar ganho de FPS antes disso.

Pendências antigas de .50 permanecem no relatório PLAYER_FLOW_FIXES_0_12_50.md:
hardware/NVTEGRA, playback longo, waits hot/progressivo e causas dos incidentes
sem trace conclusivo. Esta rodada não declara resolvido o buffering de 50 min.
