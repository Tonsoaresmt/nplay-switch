# Revisão de fluxos e rotas — Nplay Switch 0.11.0

Data: 27/09/2026. Base: branch `claude/nplay-tv-switch-review-n4drzv` (commit `134fd3f`).

## Escopo e limites

- Revisado linha a linha todo o cliente nativo: `main.c`, `screen_movie.c`,
  `player.c`, `curl_avio.c`, `api.c`, `net.c`, `catalog_fetch.c`, `update.c`,
  `store.c`, `diag.c`, `text.c` e `ui.c` (o `cJSON.c` é de terceiros).
- **TV web e backend não foram revisados.** Eles ficam em `C:/iptv`, que não
  existe neste ambiente, e o único repositório acessível à sessão é
  `Tonsoaresmt/nplay-switch`. Onde um achado depende da semântica do servidor,
  ele aparece como "confirmar no backend".
- Não há devkitPro neste ambiente: nada foi compilado nem executado. Esta é uma
  revisão estática; nenhum código foi alterado nesta rodada.

## Inventário de rotas usadas pelo Switch

| Rota | Método | Onde | Thread | Timeout (conexão/total) |
|---|---|---|---|---|
| `/api/auth/login` | POST | `do_login` | UI | 8/20 s |
| `/api/account/profiles` | GET | boot, login, Config > Trocar perfil | worker (`CatalogFetch`) | 6/25 s |
| `/api/account/me` → fallback `/api/auth/me` | GET | Config | worker | 5/15 s cada |
| `/api/account/prefs` | PUT | Preferências | **UI** | 5/20 s |
| `/api/catalog/home`, `tab-home?tab=movie\|series\|dorama`, `anime-home` | GET | landings | worker | 6/30 s |
| `/api/catalog/search-v2?q=` | GET | busca | worker | 6/25 s |
| `/api/catalog/movie/:id/info` | GET | detalhe/relacionados | worker | 6/25 s |
| `/api/catalog/series/:id` | GET | detalhe de série, troca de áudio/temporada | worker | 6/25 s |
| `/api/catalog/series/:id` | GET | Biblioteca > episódios (`load_dl_done`) | **UI** | 5/15 s |
| `/api/sync/favorites` | GET / POST / DELETE | Minha lista | GET worker; POST/DELETE **UI** | 5/20 s |
| `/api/sync/progress` | GET | Histórico | worker | 3/7 s |
| `/api/sync/progress/:id` | GET | antes de tocar | **UI** | 5/15 s |
| `/api/sync/progress` | POST | player (15 s), menu do Histórico | thread do player; menu na **UI** | 3/6 s; 5/20 s |
| `/api/sync/watchlater` | GET / POST / DELETE | Histórico, Assistir mais tarde | GET worker; POST/DELETE **UI** | 3/7 s; 5/20 s |
| `/api/stream/:id` | POST | `resolve_and_play`, renovação upstream, pós-`fail` | **UI** / supervisor | 8/20 s; 4/8 s |
| `/api/stream/:id/stop` | POST | fim da reprodução | UI | 3/6 s |
| `/api/stream/session/:sid/refresh` | POST | renovação R2 | supervisor | 4/8 s |
| `/api/stream/session/:sid/fail` | POST | troca de fonte | supervisor | 4/8 s |
| `/api/stream/session/:sid/heartbeat` | POST | a cada 20 s | thread do player | 3/6 s |
| `play_url` (`/api/play/:id` ou URL absoluta) | GET | FFmpeg / AVIO libcurl | player | 20 s + low-speed |
| `/api/accel/download/:id` | POST | torrent | **UI** | 5/20 s |
| `/api/accel/download-batch` | POST | série > Y | **UI** | 5/20 s |
| `/api/accel/jobs`, `/api/accel/jobs/:id` | GET | Biblioteca, espera de preparo | worker | 3/6 s; 2/4 s |
| `/api/accel/jobs/:id` | DELETE | Biblioteca > X | **UI** (1 chamada por episódio) | 5/20 s |
| `/api/accel/status` | GET | Config | worker | 5/15 s |
| capas (`logo`, `backdrop`) | GET | 3 workers | worker | 6/15 s, **sem bearer** |
| `api.github.com/.../releases/latest` + asset `.nro` | GET | Config > Atualizar | **UI** | 4/7 s; 15/**180 s** |

`X-Profile-Id` só é enviado para `BASE/api/*` com bearer (`net.c:20`). Isso está
correto.

## Mapa de telas

```
boot ─ token salvo? ─ não ─> LOGIN ─A─> login ─> PERFIS (obrigatório) ─> MAIN/Início
                    └ sim ─> LOADING(perfis) ─ ID confere ─> MAIN/Início
                                             └ não/erro ─> PERFIS (B = sair da conta)
MAIN (L/R: Início, Filmes, Séries, Animes, Doramas, Histórico)
  ├ A em filme ─> LOADING ─> FILME ─A─> player │ baixo ─> relacionados ─A─> FILME (pilha ≤ 6)
  ├ A em série ─> LOADING ─> SÉRIE ─A─> player (+ autoplay) │ Y preparar │ ZL/ZR áudio │ L/R temporada
  ├ A em episódio/ao vivo ─> player direto
  ├ Y / card "MAIS NO NPLAY" ─> teclado ─> LOADING ─> BUSCA ─A─> FILME/SÉRIE/player
  ├ Histórico: Continuar (A toca, X menu) │ Biblioteca ─> episódios preparados │ listas locais
  └ − ─> CONFIG: Preferências, Trocar perfil, Atualizar, Reiniciar, Fechar, Sair │ X Diagnóstico
player: resolve (/stream) ─> torrent? espera de preparo │ embed? recusa │ senão pergunta retomar ─> player_run
```

## Achados

Ordenados por impacto. Cada um traz o cenário concreto e a correção sugerida.

### Alta

**A1. Catálogo fica em "Carregando…" para sempre ao voltar da busca depois de
assistir algo.** `playback_memory_enter` (`main.c:660`) libera todas as landings.
`playback_memory_leave` (`main.c:671`) só recarrega se a tela atual for
`SC_MAIN`. Na busca, `input_search` com B (`main.c:2320`) apenas define
`g_screen = SC_MAIN` e não chama `load_landing`.
Cenário: Busca → filme → Assistir → sair → B (volta à busca) → B. A aba aparece
vazia, e o único jeito de sair é L/R. O mesmo vale para canal ao vivo ou
episódio aberto direto da busca.
Correção: criar um único `enter_main()` que faz
`g_screen = SC_MAIN; if (g_tab <= 4 && !g_land) load_landing(g_tab);` e usá-lo
em todos os pontos que voltam para MAIN (busca, config, perfis e
`detail_return_to_origin`).

**A2. Mudar "Ocultar +18" deixa a aba em branco e as outras desatualizadas.**
`save_selected_preference` (`main.c:2771`) invalida só `g_tab`, enquanto o
usuário ainda está em `SC_CONFIG`. `pump_landing` (`main.c:643`) só aplica o
resultado se a tela for `SC_MAIN`. Ao voltar com B (`main.c:2880`), `g_land`
continua NULL e a tela fica em "Carregando…". As outras 4 abas em cache e a
busca continuam mostrando o catálogo filtrado da forma antiga.
Correção: invalidar as 5 landings e `g_search`, e voltar para MAIN pelo
`enter_main()` de A1.

**A3. Cada recurso HLS abre uma conexão TCP+TLS nova.** `curl_avio_open_profile`
cria um `curl_easy_init` por playlist ou segmento (`curl_avio.c:335`). O handle
fica isolado do CURLSH e é destruído no `io_close`. Em um VOD R2 com vídeo, áudio
e legenda, isso dá um handshake mbedTLS completo a cada segmento de cada
rendition. Os recursos de metadados (playlists e segmentos `.vtt`) são baixados
de forma **síncrona dentro do `io_open`**, na mesma thread que decodifica e
desenha. Com legenda ligada, cada segmento WebVTT para o vídeo enquanto espera
RTT + TLS. É o candidato mais forte para "série engasga" e deve ser medido antes
de mexer em buffers.
Correção: manter um pool pequeno (2–4) de easy handles ociosos, protegido por
mutex e **não compartilhado entre threads ao mesmo tempo**. O CurlIO pega um
handle ao abrir e o devolve ao fechar. O reuso sequencial preserva o cache de
conexão do próprio handle sem o CURLSH que travou no hardware.

**A4. SD escrito várias vezes por segmento durante a reprodução.** Para cada
recurso HLS, `player_hls_io_open` reescreve `.nplay-player-boot.txt` duas vezes
(`player.c:112`, `player.c:119`) e `diag_player_event` faz 5–7
`fopen/append/fclose` com `mallinfo()` (abertura, alocação, http, close). Tudo
isso roda na thread do player. O trace do player também não tem rotação: em um
filme de 2 h ele passa de alguns MB, e o Diagnóstico o lê inteiro.
Correção: registrar em detalhe apenas os primeiros N recursos ou até o
`first-present`. Depois disso, gravar só erros e um resumo periódico. Limitar o
arquivo como já é feito no trace de rede.

**A5. Orçamento de recuperação é por sessão inteira, não por incidente.**
`retry_count` nunca volta a zero depois de um trecho reproduzido com sucesso
(`player.c:1572`, `player.c:1625`). Em um filme longo, a 1ª queda de Wi-Fi faz
um refresh. A **2ª queda, uma hora depois, chama `/fail`** e penaliza no
servidor uma fonte que estava boa. A 4ª queda encerra o player com erro.
Correção: zerar `retry_count` quando a nova tentativa avançar, por exemplo mais
de 60 s além do ponto em que falhou. Chamar `/fail` só quando a mesma fonte
falhar de novo logo após renovar.

### Média

**M1. Cada renovação pode abrir uma sessão nova (confirmar no backend).** Para
fontes upstream, `on_player_renew` chama `POST /api/stream/:id` a cada tentativa
(até 4 por incidente, `main.c:691`). Depois de `/fail`,
`api_fail_playback` também chama `/api/stream/:id` para completar o descritor
(`api.c:234`). Se o servidor cria uma sessão a cada POST, as anteriores só
expiram pelo TTL. Durante a recuperação isso pode estourar o limite de telas
("limite de telas" no meio da reconexão). O re-resolve depois do `/fail` também
pode devolver a mesma fonte que acabou de falhar. Confirmar em
`src/routes/stream.js`. O ideal é `/fail` devolver `delivery`/`container` e
remover a segunda chamada.

**M2. O heartbeat para durante a recuperação.** `pipeline_ready` volta a 0 a cada
tentativa (`player.c:977`, `player.c:1468`). No pior caso, as renovações somam
~40 s e a reabertura mais o probe somam ~40 s. Isso chega perto dos 90 s de TTL
e a sessão pode expirar antes do refresh. Manter o heartbeat enquanto houver
`session_id`, mesmo sem pipeline pronta.

**M3. Manifestos e legendas acima de 256 KB fazem a abertura falhar.** O perfil
`meta` pede `Range: 0-262143` e rejeita o recurso se `Content-Range` indicar mais
bytes (`curl_avio.c:390`). Playlists de filmes longos com URLs assinadas por
segmento, ou um `.vtt` único de filme muito falado, passam desse limite. Se for
a playlist de vídeo, o erro é fatal (`03 HLS sem memoria`). Crescer o buffer até
um teto maior (por exemplo 2–4 MB) quando o total vier informado.

**M4. As preferências da conta só são aplicadas depois de abrir Config.**
`load_settings_status` só roda no botão − (`main.c:3029`). Até lá,
`g_pref_autoplay = 1` e `g_pref_reduce_motion = 0` por padrão. Quem desligou o
autoplay no site continua pulando de episódio no Switch. Buscar
`/api/account/me` uma vez depois que Início terminar de carregar (sem disputar
o boot, como exigido na 0.7.1).

**M5. Nenhum tratamento de 401.** Não há checagem de 401/403 em nenhum ponto.
Com um token expirado, o boot mostra "Perfis indisponíveis". No meio da sessão,
cada tela falha com "HTTP 401" genérico. Centralizar: 401 em `/api/*` limpa o
token e volta ao LOGIN com uma mensagem clara.

**M6. Várias chamadas de rede ainda bloqueiam a thread SDL.** Os casos são
`resolve_and_play` (até 20 s de `/stream` + 15 s de progresso, sem B),
favoritos, Assistir mais tarde, preferências, menu do Histórico, `accel_start`,
`load_dl_done` (`main.c:1265`), a remoção da obra inteira na Biblioteca (um
DELETE síncrono por episódio, `main.c:2580`) e o atualizador. O atualizador
baixa ~23 MB com limite de **180 s** (`update.c:482`), ou seja, falha abaixo de
~130 KB/s e congela a tela sem mostrar progresso. Levar para o mesmo padrão
`CatalogFetch`, e no update usar `net_download_file_progress` com uma tela
própria.

**M7. Estado legado do Meruem ocupa heap desde o boot.** `store_init` carrega
`progress.json` (até 8 MB), `offline_series.json` (4 MB) e `fit_modes.json`
(2 MB) (`store.c:155-186`). Nenhuma função que usa esses dados é chamada pelo
app. Em consoles que já tiveram o Meruem instalado, isso tira heap do FFmpeg, que
já fechou por falta de memória (0.10.0: ~14 MB livres). Remover a leitura, ou no
mínimo não carregar no boot.

**M8. Confirmar "Preparar episódios" sempre anuncia sucesso.** O código de
`/api/accel/download-batch` é ignorado (`main.c:1913`). O app mostra
"N episódio(s) adicionado(s)" e vai para Histórico mesmo com HTTP 4xx/5xx ou
sem rede.

**M9. O atualizador pode sobrescrever outro homebrew.** `is_nplay_nro` aceita
qualquer `.nro` cujo nome contenha "nplay" ou "meruem" (`update.c:203`), por
exemplo `NPlayer.nro`. Validar o título NACP do arquivo encontrado antes de
substituí-lo.

### Baixa

- **L1.** Com 3 ou mais versões de áudio, ZL/ZR sempre vai para a primeira não
  atual (`main.c:2391`). A terceira versão nunca é alcançada, e ZL e ZR fazem a
  mesma coisa.
- **L2.** Login com resposta que não é JSON (bloqueio do Cloudflare, HTML 5xx)
  não mostra nenhuma mensagem (`main.c:2234`).
- **L3.** "Assistir mais tarde" só acumula: itens removidos em outro aparelho
  nunca saem da lista local (`main.c:1436`).
- **L4.** "Remover do Histórico" grava a posição 0 em vez de remover
  (`main.c:2445`). Confirmar se o backend esconde itens com posição ≤ 5 ou se
  existe um DELETE próprio.
- **L5.** `completed` do `GET /api/sync/progress/:id` é lido só como número
  (`main.c:760`, `main.c:835`). Se vier booleano, o prompt de retomar aparece em
  obras já concluídas.
- **L6.** A lista de episódios não se atualiza depois de assistir (progresso e
  "Visto" ficam como estavam até reabrir a série).
- **L7.** O filtro "Filmes" da busca exclui canais ao vivo. Eles só aparecem em
  "Tudo" (`main.c:1495`).
- **L8.** O seek inicial da retomada não confere o retorno de `av_seek_frame`
  (`player.c:969`). Se falhar, o HUD e o progresso salvo mostram a posição
  pedida, não a real.
- **L9.** Apertar B enquanto `av_read_frame` está bloqueado aciona o callback de
  interrupção (`player.c:87`). Isso pode virar erro `-5` e disparar uma
  renovação na API antes de sair.
- **L10.** `g_api_last_error` é um buffer global escrito por várias threads
  (landing, config e UI), então mensagens de erro podem se misturar entre telas.
- **L11.** Se o player fechar menos de 15 s depois de abrir, um worker de capa
  ainda em HTTP pode gravar a surface por cima de outra já pronta para o mesmo
  índice e vazar uma `SDL_Surface`.
- **L12.** Pontos a confirmar no backend: capas e `file_url` do acelerador são
  pedidos **sem** `Authorization`. O FFmpeg e o AVIO usam UA `Nplay-Switch/1.0`,
  enquanto a API usa um UA de navegador por causa do Cloudflare
  (`net.c:13`, `player.c:679`). Se uma regra de bot passar a valer para
  `/api/play` ou `/api/accel`, a reprodução quebra sem mudança no cliente.
- **L13.** Comentários desatualizados: `net.c` diz que todo request compartilha
  o CURLSH, mas os handles HLS são isolados. `curl_avio.c` fala em "PKI interna
  do sistema", mas a CA vem de `.nplay-ca.pem`.

## Fluxos verificados sem problema

- Rede e parse de busca, detalhes, perfis, favoritos, histórico, landings, jobs
  e config rodam em threads, com no máximo uma requisição ativa e uma intenção
  pendente (`begin_catalog_fetch`/`pump_catalog_fetch`). B em LOADING volta à
  origem certa.
- A pilha de relacionados (≤ 6), o retorno contextual de filme/série e a troca
  transacional de detalhe estão corretos.
- O perfil é validado contra a conta no boot antes de carregar dados pessoais. A
  troca de perfil reinicia o processo e o header só vai para `BASE/api/*`.
- TLS usa `VERIFYPEER=1`/`VERIFYHOST=2` com CA embutida em todos os handles
  (API, capas, GitHub, AVIO).
- O atualizador verifica tamanho, SHA-256 do release e cabeçalho NRO, e troca o
  arquivo por staging + rename atômico.
- A liberação de memória antes do player (landings, texturas e fila de capas) e
  a retomada depois estão corretas, exceto pelo retorno a MAIN descrito em A1/A2.
- A troca de faixas no player é transacional e o seek só limpa buffers quando o
  FFmpeg confirma.

## Ordem sugerida de correção

1. A1 + A2 (um helper `enter_main`): pequeno, sem risco para o player.
2. A5 + M2 (orçamento de recuperação e heartbeat): mudança localizada em
   `player_run`.
3. A4 (reduzir I/O de diagnóstico) e, em seguida, medir no hardware para decidir
   A3 (pool de handles) com dados de `player_stats`/trace.
4. M1 com o backend (`/fail` devolvendo descritor completo; sessão por
   dispositivo+item).
5. M4, M5, M7, M8 e M9; depois os itens de baixa.

Cada correção precisa de build ARM64 limpo e `tools/validate_release.ps1`, e do
roteiro `docs/SWITCH_0_11_HARDWARE_CHECK.md` no Switch real.
