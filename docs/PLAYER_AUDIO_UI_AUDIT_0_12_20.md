# Auditoria do player, HUD e idioma — 0.12.20

Data: 28/09/2026

## Motivo da rodada

O teste em hardware da 0.12.19 mostrou dois problemas objetivos: o player ainda
nao correspondia ao HUD criado no commit `f5296a1` e a selecao automatica podia
continuar em ingles mesmo na experiencia dublada. A 0.12.19 havia apenas
aproximado o visual antigo; ela nao continha `player_ui.c`.

## Comparacao realizada

Foram comparados:

- o player atual da linha `codex/switch-rebuild`, que contem as correcoes HLS,
  limite de memoria, NVTEGRA, diagnostico e recuperacao de sessao;
- os commits `f5296a1` (HUD PC-like) e `453d1bf` (idioma e fluidez);
- `C:/iptv/public/js/player/audio-track-preference.js` e
  `C:/iptv/scripts/hls-managed-audio-preference-test.mjs`, que definem a regra
  usada atualmente pelo site.

O `player.c` de `f5296a1` nao foi aplicado inteiro. Ele e anterior a varias
correcoes confirmadas no Switch e substituiria o transporte/recovery atual.
Foram portados o modulo visual e os contratos de apresentacao sobre o pipeline
0.12.x, preservando o caminho de reproducao ja diagnosticado.

## Causa do ingles em conteudo dublado

Ha pacotes R2 antigos com duas faixas: a primeira marcada `eng` e a segunda
marcada `und`, embora a segunda seja a dublagem preservada. A selecao anterior
considerava apenas o idioma identificado e aceitava ingles. O site ja possui uma
excecao explicita para esse contrato legado.

A politica agora e isolada em `source/audio_policy.c` e segue esta ordem:

1. idioma realmente mantido durante recuperacao ou proximo episodio;
2. versao Dublada/Legendada escolhida na tela da serie;
3. preferencia da conta (`dub`, `leg` ou `any`);
4. melhor faixa indicada pelo FFmpeg, sem escolher comentario/descritiva quando
   ha outra opcao.

Para Dublado, PT-BR identificado vence. No caso legado exato de duas faixas
ingles + desconhecida, a desconhecida vence e aparece no painel como
`Portugues (dublado)` com `PT*`. O asterisco indica inferencia do pacote legado;
o metadado original continua `und`, evitando falsificar o fluxo interno.

Para Legendado, japones/original vence quando existe; depois vem uma faixa
estrangeira identificada. Legenda PT e ativada automaticamente quando disponivel.

Escolhas locais de audio e legenda agora usam arquivos separados por perfil.
Uma preferencia antiga de outro perfil nao pode mais contaminar a sessao atual.
A continuidade carrega idioma e indice: o idioma vence se a ordem das faixas
mudar entre episodios; indice so e usado quando todas as faixas nao possuem tag.

## HUD efetivamente portado

`include/player_ui.h` e `source/player_ui.c` agora fazem parte do NRO atual:

- barra vetorial com timeline, tempo, transporte, volume e faixas;
- pausa com titulo, contexto do episodio/filme e sinopse;
- painel de audio e legendas em duas colunas, com idioma, codec, canais,
  faixa ativa e selecao separadas;
- loader e recuperacao com animacao consistente;
- buffering discreto sobre o ultimo quadro, sem o cartao antigo concorrente;
- busca analogica desenhada na propria timeline, com tempo, diferenca, capitulo,
  confirmacao por A e cancelamento por B;
- toque no unico botao de faixas corrigido para abrir o mesmo painel mostrado.

O cartao de proximo episodio existe no modulo visual, mas permanece oculto nesta
release. Exibi-lo sem integrar foco, confirmacao e retorno do player criaria uma
acao enganosa. O autoplay atual continua funcionando pelo fluxo ja existente.

## Custo e seguranca

Nomes de faixa, codecs, canais e capitulos sao preparados uma vez por tentativa
de reproducao. A primeira integracao reconstruia esses dados a cada quadro; isso
foi removido antes da release. O HUD nao cria imagens externas e reutiliza o
cache de texto existente. O aumento do NRO e pequeno e nao altera os buffers HLS.

## Validacao automatizada

`tools/test_audio_policy.c` cobre:

- PT-BR explicito;
- pacote legado `eng + und`;
- titulo `Dublado Nacional` sem tag;
- anime PT/JPN em Dublado e Legendado;
- quatro idiomas fora de ordem;
- comentario de diretor e audio descritivo;
- apenas idiomas estrangeiros;
- ordem de faixas trocada entre episodios;
- continuidade de faixa sem metadado;
- preferencia `any` e versoes Dublada/Legendada/Dual.

`tools/validate_release.ps1` compila o NRO do zero, executa a politica de audio
com `-Werror`, os contratos de player/API/episodios/remux, confere TLS, simbolos
FFmpeg/NVTEGRA e verifica que o HUD usa metadados precomputados.

## Teste obrigatorio no Switch

Compilacao e testes host nao substituem o hardware. Validar nesta ordem:

1. filme R2 com duas faixas antigas: deve iniciar em portugues quando Dublado;
2. Configuracoes em Legendado: audio original e legenda PT quando existirem;
3. serie com versoes: selecionar Dublada/Legendada e abrir dois episodios;
4. trocar audio manualmente, deixar o episodio terminar e conferir continuidade;
5. pausar: conferir titulo, contexto, sinopse e controles sem cortes em 1280x720;
6. Y/X: navegar nas duas colunas, aplicar e cancelar;
7. analogico: segurar 550 ms, mover a timeline, cancelar e confirmar;
8. simular rede lenta: spinner deve ficar sobre o ultimo quadro e B deve sair.

Se o idioma ainda estiver errado, fotografar o painel de faixas. O trace agora
registra `streams selected` com indice, idioma normalizado, preferencia e total
de faixas, sem gravar URL assinada.

## Continuidade para outro agente

- Base de trabalho: branch `codex/switch-0.12.18`, derivada de
  `origin/codex/switch-rebuild`.
- Nao substituir `source/player.c` pelo arquivo de `f5296a1`; portar somente
  recursos isolados sobre o supervisor atual.
- Arquivos centrais: `source/player.c`, `source/player_ui.c`,
  `source/audio_policy.c`, `source/main.c` e `tools/validate_release.ps1`.
- Pendencia deliberada: integrar o cartao/foco de proximo episodio somente com
  um retorno explicito do player e teste de autoplay; hoje ele esta oculto.
- Pendencia externa: teste no Nintendo Switch real conforme o roteiro acima.
