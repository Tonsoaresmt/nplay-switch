# Player: matriz ampliada e protecoes de render — candidata .57

Base local `4a53f67` (.56). Pedido: verificar muitos cenarios e solucoes para
deixar o player o mais funcional/estavel possivel, sem publicacao solicitada.
Checkout permanece `switch-access-expiry`, branch `codex/switch-player-hardening`.

## Defeitos encontrados e correcoes

1. **Quadro e textura de tamanhos diferentes.** A textura usa as dimensoes
   iniciais do decoder. Um quadro YUV420P/NV12 posterior menor podia passar
   direto para o upload completo. O harness executou o bloco C da .56 e
   rejeitou stride/plano insuficiente no caso 1920x1080 -> 640x360.
   Isso demonstra uso incorreto dos buffers nessa condicao, nao prova que
   foi a causa de um crash especifico de cliente.
   Agora dimensoes divergentes passam pelo conversor para o tamanho fixo da
   textura; o alvo de alocacao nao cresce a cada alteracao da fonte.
2. **Fallback NV12 refeito por quadro.** Se a GPU/renderer nao cria NV12,
   o caminho antigo tentava novamente em todo quadro e criava outra IYUV,
   descartando a anterior. Agora a recusa fica memorizada por pipeline.
   Teste de 100 quadros confirma uma tentativa NV12 e uma textura reutilizada.
3. **Proporcao de exibicao.** O letterbox ignorava sample aspect ratio e
   ficava preso a geometria inicial. O retangulo agora acompanha o quadro e
   SAR: anamorfismo 720x480/8:9, PAL 720x576/16:15, retrato e panoramico.
   As coordenadas dos letreiros acompanham o mesmo retangulo. O HUD segue720p.
4. **Erros do conversor.** Retorno sem linhas/erro do sws_scale agora falha
   explicitamente, sem apresentar um buffer antigo como quadro novo.
5. **Busca em video muito curto.** Duracao conhecida <=1s agora limita o
   destino a zero; nao e confundida com duracao desconhecida para permitir
   saltos de 10/60s para fora do clipe. Controle opt-in da .56 preservado.

## Cobertura executavel

| Camada | Cobertura | Tipo de evidencia |
|---|---|---|
| Controles | 200.000 passos, 5.000 confirmacoes, 5.000 cancelamentos, 10.000 casos de modal/fonte sem busca | Dispatcher/passos/prefs C extraidos; SDL e tempo simulados |
| Analogico OFF | Todos os 65.536 valores do eixo; neutralizacao/hold/modal/wrap quando ON | Helper C real; nao mede drift fisico |
| Upload | 1.152 transicoes de tamanho/formato, falhas e limpeza | Bloco C real; FFmpeg/SDL instrumentados, nao GPU |
| Midia | 55 casos, 6 perfis, Range/HLS posicionado/remux, HTTP atrasado e erros | FFmpeg CLI real + HTTP local + trimmer HLS C real |
| Ciclo do player | Pausa, retomada, seek, audio/legenda, HOME, readmissao, falha/cancelamento, episodios | Suites existentes obrigatorias; pipeline/conta simulados |
| Rede/legendas | TLS, backpressure, bytes/cancel, 404/503, corpo parcial, faixas e letreiros | Suites existentes; ha partes com libcurl HTTPS real e partes simuladas |

`tools/test_seek_controls.mjs` usa xorshift deterministico. Tempos proximos
do wrap, duracao desconhecida, 0,5s/1s/1min/24min/2h, pausado/tocando e
mudancas de contexto compoem a matriz. Nao sao 200 mil plays reais.

`tools/test_video_upload.mjs` extrai o caminho efetivamente usado pelo NRO.
Combina seis tamanhos (inclui 641x361 impar e retrato) e YUV420P, NV12,
NVTEGRA com transferencia simulada e formato convertido de 10bits. Os
buffers sao instrumentados por plano/stride/capacidade. Testa OOM, contexto
SWS, writable, conversao, textura, upload, transferencia e teardown.
`--baseline-056` deve falhar: e uma reproducao negativa, nunca teste verde.

`tools/test_player_scenario_matrix.mjs` gera clips sinteticos de12s:

| Perfil | Video | Audio AAC | SAR |
|---|---|---|---|
| 360p | 640x360 /24fps | 44,1kHz mono | 1:1 |
| 480p | 720x480 /29,97fps | 48kHz stereo | 8:9 |
| 720p | 1280x720 /23,976fps | 44,1kHz stereo | 1:1 |
| 1080p | 1920x1080 /30fps | 48kHz stereo | 1:1 |
| 1080p60 | 1920x1080 /60fps | 48kHz 5.1 | 1:1 |
| Retrato | 720x1280 /30fps | 48kHz stereo | 1:1 |

Cada perfil decodifica inicio/meio/retorno por Range, tres playlists HLS
cortadas pelo **mesmo C hls_media_playlist_trim do app** (sem seek interno
HLS), e dois reinicios de remux chunked sem Range. Video e audio sao mapeados.
Depois ha atraso inicial400ms, entrega em blocos a cada10ms, 503 recuperavel,
403/404/416 permanentes (uma requisicao cada) e servidor que ignora Range.

**Achado importante do teste negativo:** FFmpeg CLI pode sair com codigo0
mas zero quadros quando o servidor ignora Range. Codigo de processo sozinho
nao comprova reproducao. A matriz classifica esse caso como nao reproduzivel;
os guards do NRO de preroll/EOF/primeiro quadro continuam obrigatorios.
Isso nao torna uma fonte sem suporte a Range buscavel nem reinicia em zero.

Resultados gerados: `build/player-scenarios/results.json`. As fixtures usam
FFmpeg CLI8.1.1; o NRO e compilado com FFmpeg7.1. Nao confundir esses ambientes.
O endpoint de remux e sintetico, nao uma sessao TorBox real. O manifesto HLS
e sintetico, nao um pacote da producao/R2.

## Fontes oficiais e decisoes

SDL atualiza a textura inteira quando rect=NULL, e recebe strides separados:
[UpdateYUVTexture](https://wiki.libsdl.org/SDL2/SDL_UpdateYUVTexture),
[UpdateNVTexture](https://wiki.libsdl.org/SDL2/SDL_UpdateNVTexture).
Dimensoes/strides/SAR pertencem ao quadro:
[AVFrame FFmpeg7.1](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavutil/frame.h).
A conversao retorna linhas produzidas:
[swscale FFmpeg7.1](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libswscale/swscale.h).

## O que ainda NAO esta comprovado

- Fluidez NVTEGRA/GPU em1080p60, HEVC, HDR/10bits e quatro horas de sessao.
  Decodificar no PC nao homologa capacidade/desempenho do Switch.
- Uma mudanca de resolucao usa SWS para conservar o tamanho reservado. Isso
  tem custo de CPU nessa condicao; sem hardware nao quantificar o impacto.
  Caminho direto de resolucao constante permanece direto.
- Wi-Fi real, latencia do provedor, vencimento de URL depois de50min e troca
  de episodio de titulos especificos ainda exigem trace/teste no aparelho.
- Sem Linux host integrado, conta de producao, deploy ou reprocessamento.
  Fontes hot legadas sem contrato de busca continuam sem saltos.

## Validacao final e teste fisico

Build ARM64 completo `make -B -j4` passou com `-Werror`, sem avisos.
`tools/validate_release.ps1 -SkipBuild` passou completo, SEM SkipMediaFixtures,
no artefato final: inclui novas suites, todos os guards anteriores e simbolos.
Matriz final:55 casos/6perfis/118pedidos HTTP; uploads:1152; controles:200000.
NRO: **24.291.151 bytes**, SHA-256:
`85ade00f99127a22600fbdfd8e629d3df426629dc4af93d996dd264779e63e23`.
`git diff --check` passou. Nenhuma mudanca do app/rebuild apos essa bateria.
Sidecar local atualizado. Sem push/release/backend nesta rodada.

No console, testar par/Pro Controller e touch, OFF/ON por perfil, salto para
meio e retorno pausado/tocando em R2/Range/restart; trocar faixas e episodios,
HOME/repouso e sessao de60+min. Conferir imagem/SAR/letreiros e guardar logs
do proprio titulo que falhar. Nao publicar como homologado antes dessa prova.
