# TV antiga: só áudio, imagem parada no detalhe do filme

Data: 27/09/2026. Relato: em algumas TVs mais antigas, depois de apertar Assistir,
o áudio do filme toca, mas a tela continua mostrando o detalhe do filme (capa e
sinopse), parada.

## Atualização 28/09/2026: causa encontrada no código da TV

Com acesso ao `Tonsoaresmt/Nplay`, a causa foi confirmada. Ela é a hipótese T5
(o player não ocupava a tela), mas por um motivo de CSS: o player do shell
legado (`public/tv/legacy-shell-v2.js`, usado em Chromium < 80), a página antiga
`/tv` (`public/tv.html`) e o `#player-modal` do shell moderno eram posicionados
só com `inset:0`, que existe apenas a partir do Chromium 87. Sem essa
propriedade, o player colapsava fora da tela: 0×0 no legado, 300×150 no `/tv`,
0×0 no moderno em Chromium 80–86. O áudio tocava, o detalhe continuava visível
e as teclas iam para o player invisível. Reproduzido no Chromium removendo
`inset`.

A correção, com teste de regressão, está na branch
`claude/nplay-tv-switch-review-n4drzv` do `Tonsoaresmt/Nplay`, commit `5b24542`.
Ela não foi publicada: push no `main` publica em produção e precisa de
aprovação. Falta homologar na TV real. A sonda `tools/tv_probe.html` continua
útil se aparecer outro sintoma.

## Limites desta investigação

- O código da TV web fica em `C:/iptv`. Ele não está em nenhum repositório
  acessível a esta sessão (`Tonsoaresmt/Nplay` e `Tonsoaresmt/iptv` recusaram
  acesso). A política de rede deste ambiente também bloqueia
  `nplay.tonserverlocal.uk`. **Nada do código da TV foi lido.**
- A análise usa o contrato do backend documentado neste repositório (AGENTS.md,
  `tools/build_playback_investigation_doc.py`, `tools/probe_r2_packages.mjs`) e o
  formato real dos pacotes R2 medido em rodadas anteriores.
- Para fechar o diagnóstico no aparelho, foi criada a sonda `tools/tv_probe.html`
  (seção 6). Ela foi validada como ES5 puro e executada no Chromium deste
  ambiente contra um backend simulado. As TVs antigas reais ainda precisam
  rodá-la.

### Não é o app do Switch

No Switch, antes do primeiro quadro, `resolve_and_play` desenha "PREPARANDO" e
o player desenha "PREPARANDO VIDEO". Esses dois quadros sempre substituem a tela
de detalhe, então o sintoma não se encaixa no NRO atual, esteja ele no dock ou
não. Se aparecer no Switch, o `Nplay.nro` instalado é antigo.

## 1. O que o sintoma já prova

1. **As rotas funcionaram até o áudio.** Ouvir o áudio do filme exige
   `POST /api/stream/:id` com 200, `GET /api/play/:id` com 302, master HLS,
   playlist de áudio e segmentos de áudio servidos. Autenticação, sessão, limite
   de telas, token curto e token de mídia do áudio estavam válidos.
2. **Só a TV antiga falha com o mesmo título.** Então a causa principal é
   capacidade do aparelho (motor web, decodificador, compositor), não conteúdo
   ou backend. Exceção possível: a playlist de vídeo recusada só para aquela TV
   (seção 5, linhas de R2).
3. **Ver o detalhe parado, e não uma tela preta do player, é o dado mais
   importante.** Se o problema fosse só o vídeo não decodificar, o app teria
   trocado para a tela do player (preta, com controles). Continuar vendo a
   sinopse significa que **a camada web não foi repintada depois do Play**.
   Sobram três caminhos: exceção de JavaScript, página travada, ou o vídeo
   desenhado por baixo do detalhe.

## 2. Hipóteses, da mais provável para a menos provável

### T1. Exceção de JavaScript logo depois de `video.play()`

A sequência típica de um player web é: cria `<video>`, define `src` ou
`hls.attachMedia`, chama `play()`, **e só depois** esconde o detalhe e mostra o
player. Se algo lança erro entre esses passos, o áudio já começou e a tela nunca
muda. Isso casa exatamente com o relato. APIs que não existem nos motores de
2014–2018 (Tizen 2.4–4.0, webOS 3–4, Chromium 38–56):

| Código comum | Chromium mínimo | Efeito em TV antiga |
|---|---|---|
| `video.play().then(...)` / `.catch(...)` | 50 | `play()` devolve `undefined` e dá TypeError |
| `el.requestFullscreen().catch(...)` | 71 (Promise) | método ausente/prefixado, ou devolve `undefined` |
| `AbortSignal.timeout()` | 103 | TypeError no fetch de progresso/heartbeat |
| `Promise.prototype.finally` | 63 | TypeError |
| `Array.prototype.at`, `String.replaceAll`, `structuredClone` | 92 / 85 / 98 | TypeError |
| `ResizeObserver`, `IntersectionObserver` | 64 / 51 | ReferenceError |
| `?.`, `??`, `async` sem transpilar | 80 / 55 | o módulo do player nem carrega |

**Como confirmar:** a tabela da sonda mostra `video.play() devolve Promise`,
`Fullscreen` e cada API. No app, adicionar um painel global de erros:
`window.onerror` + `unhandledrejection` escrevendo na tela e em
`POST /api/diag`, se existir.

**Correção:** envolver `play()` com `var r = v.play(); if (r && r.then) r.catch(...)`,
trocar a ordem para **mostrar o player antes de chamar `play()`**, prefixar o
fullscreen e transpilar o bundle com Babel ou `@vitejs/plugin-legacy` para as
TVs-alvo.

### T2. A página para de repintar (thread principal ou compositor saturado)

O áudio é decodificado fora da thread da página. Se o JavaScript ou o
compositor ficam ocupados, o som continua e a tela congela no último quadro
pintado, que é o detalhe. Em TVs antigas com vídeo 1080p isso acontece por:

- hls.js sem Web Worker (`enableWorker: false`, ou Worker indisponível)
  processando segmentos grandes na thread da UI;
- CSS pesado sobre o vídeo: `backdrop-filter`, `filter: blur`, sombras e
  gradientes grandes, `transform`/`opacity` animados no detalhe ou no HUD;
- catálogo e detalhe mantidos no DOM (apenas cobertos) durante a reprodução;
- timers de progresso/HUD a cada 100–250 ms re-renderizando componentes.

**Como confirmar:** durante o teste da fonte Nplay, a linha "UI: fps / maior
travamento" da sonda congela ou mostra travamentos de segundos.

**Correção:** `display:none` no catálogo e no detalhe enquanto o player está
aberto; nenhum blur ou sombra no player; `enableWorker: true`; atualizar o HUD
no máximo 1 vez por segundo; limitar a resolução nas TVs fracas
(`capLevelToPlayerSize`, `maxMaxBufferLength` menor).

### T3. O formato do pacote R2 excede a TV antiga

Formato conhecido dos pacotes R2: HLS **fMP4/CMAF** (`EXT-X-MAP`, `.m4s`),
H.264 **1920x1080**, **áudio em rendition separada** (2 faixas AAC), legendas
WebVTT e **master sem atributo `CODECS`** (AGENTS 0.9.2).

- HLS nativo de TVs até ~2017 costuma aceitar só segmentos MPEG-TS (HLS v3–v6).
  Com `EXT-X-MAP`/fMP4 e áudio separado, alguns tocam a rendition de áudio e
  descartam o vídeo.
- Sem `CODECS`, o player nativo não sabe se a variante tem vídeo decodificável.
  Alguns escolhem só o grupo de áudio.
- H.264 High acima do nível 4.1, 1080p60 ou bitrate alto passa do decodificador
  de TVs 2013–2016.

Sozinho, T3 daria tela preta com o player aberto. Combinado com T1/T2 (erro ou
travamento na hora de montar o vídeo), dá exatamente o relato.

**Como confirmar:** compare, na mesma TV, os botões da sonda "HLS TS (legado)",
"HLS fMP4 + áudio separado" e "Fonte Nplay". A combinação TS OK, fMP4 com
`AUDIO SEM VIDEO` e Nplay com `AUDIO SEM VIDEO` fecha T3. As linhas
`MSE H.264 High 4.0/4.2` mostram o limite do decodificador.

**Correção (empacotador/backend):** gravar `CODECS="avc1.64002x,mp4a.40.2"`,
`RESOLUTION` e `FRAME-RATE` no master; publicar um degrau de 720p H.264 Main 3.1
com cerca de 2,5 Mbps; para TVs sem fMP4, gerar uma variante TS ou forçar
hls.js (MSE) no lugar do HLS nativo. **No app:** preferir hls.js quando
`MediaSource` existe, e só usar HLS nativo quando não houver MSE.

### T4. Decodificador de hardware ocupado por outro `<video>`

TVs antigas têm **um** decodificador de vídeo. Se o hero, o detalhe ou um card
tiver trailer ou prévia em `<video>` que não foi liberado, o player recebe só o
áudio (decodificado em software) e a imagem congela.

**Como confirmar:** ligar "Dois vídeos: sim" na sonda. Se o segundo fica com 0
quadros, a TV só suporta um decodificador. No app, verificar se existe trailer
ou prévia no detalhe.

**Correção:** antes do player, em todo vídeo de prévia chamar
`pause(); removeAttribute('src'); load();`.

### T5. O vídeo é desenhado por baixo do detalhe

Em Tizen e webOS, o `<video>` nativo vai para um plano de hardware **atrás** da
página, e o navegador abre um "buraco" transparente na área do vídeo. Se o
detalhe continua no DOM com fundo opaco e fica acima do player, ou se um
ancestral do player tem `transform`/`opacity`/`filter` que quebra o buraco, a TV
mostra o detalhe e toca o som.

**Correção:** player como filho direto de `body`, sem ancestrais com
transform/opacity/filter, fundo transparente, e detalhe com `display:none`.

### T6. A fonte escolhida é `embed` ou `torrent`

Um `iframe` de embed em TV antiga pode tocar áudio atrás da interface. **Como
confirmar:** o teste "Fonte Nplay" da sonda mostra o `container` escolhido.
**Correção:** tratar `embed` como indisponível na TV antiga, como o Switch já faz.

## 3. Onde procurar no código da TV (`C:/iptv`)

```text
grep -rnE "\.play\(\)\.(then|catch)|requestFullscreen\(\)\.|AbortSignal\.timeout|\.finally\(|replaceAll\(|\.at\(-?[0-9]|structuredClone" public src
grep -rnE "enableWorker|new Hls\(|canPlayType\(.application/vnd.apple.mpegurl" public src
grep -rnE "backdrop-filter|filter:\s*blur|will-change|transform:" public/css
grep -rnE "<video|autoplay|muted loop|trailer|preview" public src
```

Conferir também:

- a ordem "mostrar player → `play()`";
- o detalhe escondido com `display:none` enquanto o player está aberto;
- se o bundle é transpilado para ES5 (Babel/`plugin-legacy`);
- se o master R2 gerado tem `CODECS`.

## 4. Correções priorizadas para a TV

1. Painel global de erros (`window.onerror`/`unhandledrejection`) visível na TV,
   para transformar "tela parada" em mensagem.
2. Trocar para a tela do player **antes** de `play()` e proteger toda chamada que
   depende de Promise.
3. `display:none` no catálogo e no detalhe durante a reprodução; remover
   blur/sombras do player; hls.js com worker.
4. Liberar qualquer `<video>` de prévia antes de abrir o player.
5. Empacotador: `CODECS`/`FRAME-RATE` no master, degrau 720p e variante
   compatível para TVs sem fMP4.
6. Timeout de primeiro quadro: se `currentTime` avança e `videoWidth === 0`
   depois de 8 s, mostrar "Esta TV não suporta este vídeo" e pedir
   `/api/stream/session/:sid/fail` ou uma qualidade menor.

## 5. Matriz de rotas e erros (TV e Switch)

Legenda da coluna "Fonte": **C** = confirmado em rodada anterior (AGENTS, sondas
ou auditoria); **P** = possível pelo contrato, confirmar no backend.

### Conta e catálogo

| Rota | Erro | Fonte | Causa provável | TV deve | Switch hoje |
|---|---|---|---|---|---|
| `POST /api/auth/login` | 401/400 | P | credenciais | mensagem da API | mostra `error` |
| | 403/409/429 | P | plano, conta, limite de dispositivos | mensagem da API | mostra `error` |
| | HTML/403 do Cloudflare | P | regra de bot/UA | "servidor recusou a conexão" | **não mostra nada** |
| `GET /api/account/profiles` | 401 | C (401 sem token) | token expirado | voltar ao login | "Perfis indisponíveis" |
| `GET /api/account/me` (+ `/api/auth/me`) | 401/5xx | P | token, servidor | manter última conta | "Plano indisponível" |
| `PUT /api/account/prefs` | 400/401/5xx | P | valor inválido, token | desfazer e avisar | desfaz e avisa |
| `GET /api/catalog/home`, `tab-home?tab=…`, `anime-home` | 502/504 | C (Séries) | consulta lenta no backend ou gateway | tentar de novo, manter cache | "Falha em Séries: HTTP 502" |
| | timeout | C | payload grande | carregando + tentar de novo | limite de 30 s em thread |
| `GET /api/catalog/search-v2?q=` | 400 / vazio | P | consulta vazia ou longa | "nada encontrado" | toast |
| `GET /api/catalog/movie/:id/info` | lento até ~7 s | C | enriquecimento TMDB | spinner cancelável | thread cancelável |
| | 404 | P | item removido | voltar | volta à origem |
| `GET /api/catalog/series/:id` | 404/timeout | P | série removida | voltar | volta à origem |
| `/api/sync/favorites` GET/POST/DELETE | 401/404/409 | P | token, item, duplicado | reverter estado | DELETE ignora erro |
| `/api/sync/progress` GET, GET/:id, POST | 404/401 | P | sem progresso ou token | tocar do início | trata como sem progresso |
| `/api/sync/watchlater` GET/POST/DELETE | 401/5xx | P | token ou rede | manter local | avisa e mantém local |

### Reprodução

| Etapa / rota | Erro | Fonte | Causa provável | TV deve | Switch hoje |
|---|---|---|---|---|---|
| `POST /api/stream/:id` | 401 | C | token | login | toast com HTTP |
| | 403 | P | plano, +18, perfil infantil | mensagem da API | toast com HTTP |
| | 404 | C (15 episódios prontos sem fonte ativa) | item sem fonte ativa | "indisponível" | toast |
| | 409/429 "limite de telas" | C (mensagem) / P (código) | telas ou limite diário | mensagem + encerrar outras sessões | toast |
| | 200 `container=embed` | C | só há embed | "indisponível neste aparelho" | recusa |
| | 200 `container=torrent` | C | precisa do acelerador | tela de preparo | espera o acelerador |
| `GET /api/play/:id?token` | 502 | C | `resolveStreamUrl` não renovou nenhuma fonte | `/fail` ou mensagem | recupera e depois `/fail` |
| | 401/403 | P | token curto expirou (demora entre stream e play) | re-resolver | recupera |
| | 404 | P | sessão encerrada ou expirada | re-resolver | recupera |
| | 504 | P | gateway | tentar de novo | recupera |
| Master R2 | 404 | C (8 de 100 amostras) | ponteiro publicado sem objeto | `/fail` | `/fail` na 2ª tentativa |
| | 403 | P | token de mídia expirado ou fora do prefixo | refresh | refresh |
| | manifesto gzip + `Content-Range` comprimido | C | CDN comprime e quebra quem manda `Range` | não mandar `Range` no manifesto | contornado (0.7.4) |
| | sem `CODECS` | C | empacotador | ver T3 | força H.264/AAC |
| Playlists filhas / init / segmentos | 404 só no vídeo | P | pacote incompleto | `/fail` | erro -5, recupera |
| | 403 no meio do filme | P | token de mídia expirou | refresh | refresh |
| | sem CORS | P | R2 sem `Access-Control-Allow-Origin` para o site | quebra hls.js/MSE (o nativo ignora) | não se aplica |
| Decodificação na TV | tempo avança com `videoWidth=0` | este relato | T1–T5 | ver seção 4 | não se aplica |
| `POST /api/stream/session/:sid/heartbeat` | 404/410 | P | sessão expirou (TTL ~90 s) | re-resolver ao retomar | ignora; M2 da revisão |
| `POST /api/stream/session/:sid/refresh` | 404/502 | P | sessão ou fonte | `/fail` | próxima tentativa |
| `POST /api/stream/session/:sid/fail` | 404/409 | P | nenhuma fonte alternativa | mensagem final | erro final |
| `POST /api/stream/:id/stop` | 404 | P | já encerrada | ignorar | ignora |

### Biblioteca (acelerador)

| Rota | Erro | Fonte | Causa provável | TV deve | Switch hoje |
|---|---|---|---|---|---|
| `POST /api/accel/download/:id` | 401/403/5xx | P | plano sem acelerador, falha | mensagem | toast |
| `POST /api/accel/download-batch` | qualquer erro | P | idem | mensagem | **anuncia sucesso** (M8 da revisão) |
| `GET /api/accel/jobs`, `/jobs/:id` | estado `erro`/`failed`/`cancelled` | C | job falhou | mostrar erro do job | mostra |
| `DELETE /api/accel/jobs/:id` | 404 | P | já removido | ignorar | ignora |
| `file_url` do job | 401/403 | P | se exigir bearer, o player não envia | player com cabeçalho ou URL assinada | não envia bearer |
| Capas (`logo`/`backdrop`) | 404/lento | P | imagem ausente | inicial do título | inicial do título |

## 6. Como usar a sonda `tools/tv_probe.html`

1. Copiar para o site, por exemplo `C:/iptv/public/tv-probe.html`. Precisa ser o
   mesmo domínio do Nplay para o teste "Fonte Nplay" usar a sessão já logada
   (ele procura o token no `localStorage` e mostra só o nome da chave).
2. Na TV antiga, entrar no Nplay uma vez e depois abrir
   `https://<site>/tv-probe.html?item=<ID do filme>&auto=1`.
3. Se o site usa o próprio hls.js, acrescentar `&hlsjs=/caminho/hls.min.js`. Sem
   isso a sonda baixa o hls.js 1.5 do jsDelivr e, se falhar, a versão legada 0.14.
4. Rodar também "HLS TS (legado)", "HLS fMP4 + áudio separado" e "Dois vídeos:
   sim". Os três testes usam vídeos públicos de terceiros; se algum sair do ar,
   usar `?src=`.
5. Fotografar o veredito, a tabela de capacidades e o registro.

| Veredito | Significa | Hipóteses |
|---|---|---|
| VIDEO OK nesta TV (fonte Nplay) | A TV decodifica o pacote; o problema está no app | T1, T2, T4, T5 |
| AUDIO SEM VIDEO (fonte Nplay), TS OK | Formato do pacote | T3 |
| AUDIO SEM VIDEO só com "Dois vídeos" | Um decodificador só | T4 |
| Backend recusou: HTTP xxx | Ver a matriz da seção 5 | — |
| container embed/torrent | Fonte inadequada para a TV | T6 |
| Números da linha "UI" congelam durante o teste | Página travada | T2 |

Validado neste ambiente com o Chromium do Playwright (backend simulado): parse
ES5 estrito (acorn `ecmaVersion: 5`), zero erros JS, vídeo OK, áudio sem vídeo,
fluxo `/api/stream` → `/api/play` → 302 e recusa HTTP 429 com a mensagem da API.
O caminho hls.js não foi exercitado aqui porque a rede bloqueia o CDN.
