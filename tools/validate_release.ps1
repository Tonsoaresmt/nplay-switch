param([switch]$SkipBuild)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Assert-True([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$makeVersion = [regex]::Match((Get-Content Makefile -Raw), 'APP_VERSION\s*:=\s*([0-9.]+)').Groups[1].Value
$headerVersion = [regex]::Match((Get-Content include/update.h -Raw), 'APP_VERSION_STR\s+"([0-9.]+)"').Groups[1].Value
Assert-True ($makeVersion -and $makeVersion -eq $headerVersion) "Versoes divergentes: Makefile=$makeVersion update.h=$headerVersion"

$sources = (Get-Content source/net.c,source/curl_avio.c,source/player.c -Raw) -join "`n"
Assert-True ($sources -notmatch 'CURLOPT_SSL_VERIFYPEER\s*,\s*0L') 'SSL_VERIFYPEER inseguro encontrado.'
Assert-True ($sources -notmatch 'CURLOPT_SSL_VERIFYHOST\s*,\s*0L') 'SSL_VERIFYHOST inseguro encontrado.'
Assert-True ($sources -notmatch 'tls_verify"\s*,\s*"0') 'tls_verify inseguro encontrado.'
Assert-True ($sources -match 'CURLOPT_CAINFO') 'libcurl nao recebe um bundle CA explicito no Switch.'
Assert-True ($sources -match 'net_configure_curl_isolated\(c->easy\)') 'AVIO libcurl compartilha conexoes longas ou nao usa a cadeia CA embutida.'
Assert-True ($sources -notmatch 'request->userdata\s*=') 'O player voltou a sobrescrever userdata do chamador.'
Assert-True ($sources -match 'AVIOContext \*avio = NULL') 'MP4 remoto voltou ao AVIO por blocos que falha no Switch.'
Assert-True ($sources -match 'avformat_open_input\(&fmt, url, forced_format') 'Fonte remota nao usa o caminho de abertura validado.'
Assert-True ($sources -match 'CURLOPT_ACCEPT_ENCODING, "identity"') 'Ranges do MP4 podem ser alterados por compressao HTTP.'
Assert-True ($sources -match 'fmt->io_open = player_hls_io_open') 'HLS voltou a depender do HTTPS interno do FFmpeg/libnx.'
Assert-True ($sources -match 'nplay_curl_avio_open_hls') 'Playlists e segmentos HLS nao usam o transporte libcurl.'
Assert-True ($sources -match 'forced_format = av_find_input_format\("hls"\)') 'Manifesto raiz voltou a passar pelo probe que fecha o processo no Switch.'
Assert-True ($sources -match 'fmt->pb = avio') 'Manifesto HLS raiz nao e fornecido explicitamente ao demuxer.'
Assert-True ($sources -match 'avformat_open_input\(&fmt, url, forced_format') 'FFmpeg voltou a sondar automaticamente o manifesto HLS raiz.'
Assert-True ($sources -match 'HLS_META_INITIAL \(64 \* 1024\)' -and $sources -match 'HLS_META_MAX\s+\(4 \* 1024 \* 1024\)') 'Manifestos HLS perderam o crescimento limitado de memoria.'
Assert-True ($sources -match 'CURLOPT_RANGE, NULL') 'Manifestos HLS voltaram a pedir Range sobre texto reescrito.'
Assert-True ($sources -match 'HLS_MEDIA_RINGCAP \(4 \* 1024 \* 1024\)' -and $sources -match 'c->streaming \? producer_stream : producer') 'Segmentos HLS perderam o fluxo continuo com buffer limitado.'
Assert-True ($sources -match 'c->streaming \? wr_ring : wr_tmp' -and $sources -match 'if \(start == 0\) curl_easy_setopt\(c->easy, CURLOPT_RANGE, NULL\)') 'Segmentos HLS voltaram a usar varios ranges sem cache no R2.'
Assert-True ($sources -match 'metadata-ready') 'Manifestos HLS voltaram a ser entregues enquanto a thread ainda os altera.'
Assert-True ($sources -match '"meta", 1\)') 'Metadados HLS nao usam mais a abertura sincrona e imutavel.'
Assert-True ($sources -match 'if \(c->write_overflow\) return -2') 'Bloco HTTP truncado pode voltar a ser entregue ao FFmpeg.'
Assert-True ($sources -match 'nplay_curl_avio_stats') 'Diagnostico nao informa recursos HLS e memoria reservada.'
Assert-True ($sources -match 'http_persistent", "0"') 'HLS customizado tentou reutilizar um AVIO como protocolo HTTP nativo.'
Assert-True ($sources -match 'fmt->video_codec_id = AV_CODEC_ID_H264') 'R2 sem CODECS voltou a exigir sondagem completa de video.'
Assert-True ($sources -match 'fmt->audio_codec_id = AV_CODEC_ID_AAC') 'R2 sem CODECS voltou a exigir sondagem completa de audio.'
Assert-True ($sources -match 'SDL_UpdateNVTexture') 'Player perdeu o upload NV12 direto do decoder por hardware.'
Assert-True ($sources -match 'attempt\.playback = active') 'Tentativa recuperada nao recebe o descritor atualizado.'
Assert-True ($sources -notmatch 'seamless_reopen') 'Recuperacao interna por goto voltou a competir com player_run.'
Assert-True ($sources -notmatch 'req->renew_cb\(&req->playback') 'Decodificador voltou a renovar sessao fora do supervisor.'
Assert-True ($sources -match 'SDL_JoystickGetButton\(watch->joy, JOY_B\)') 'Preparacao do player nao pode ser cancelada por B.'
Assert-True ($sources -match 'pipeline_ready') 'Heartbeat pode voltar a disputar rede durante a abertura.'
Assert-True ($sources -match 'retry_limit = startup_failure \? 2 : 3') 'Falha inicial voltou a encerrar antes de tentar a fonte alternativa.'
Assert-True ($sources -match 'player_boot_stage\("03 abrindo fonte"\)') 'Crash do player voltou a nao deixar diagnostico persistente.'
Assert-True ($sources -match 'sdmc:/switch/\.nplay-player-boot\.txt') 'Diagnostico de crash depende de uma subpasta opcional.'
Assert-True ($sources -match 'diag_player_begin') 'Trace persistente nao e iniciado para cada reproducao.'
Assert-True ($sources -match 'first-frame') 'Trace nao distingue falha anterior ao primeiro frame.'
Assert-True ($sources -match 'first-present') 'Trace nao confirma a primeira apresentacao no renderer.'
Assert-True ($sources -match 'lang_norm' -and $sources -match 'stream_norm') 'Selecao de idioma voltou a comparar tags inconsistentes diretamente.'
Assert-True ($sources -match 'audio_policy_choose') 'Selecao de audio voltou a ficar acoplada ao player e sem testes.'
Assert-True ($sources -match 'pui_draw\(' -and $sources -match 'pui_draw_loading\(') 'Player nao usa o HUD modular nas telas de reproducao e abertura.'
Assert-True ($sources -match 'attempt\.audio_hint = last_audio') 'Recuperacao de sessao nao preserva a faixa de audio.'
Assert-True ($sources -match 'attempt\.audio_hint_language = last_audio_language') 'Recuperacao preserva indice, mas pode trocar de idioma.'
Assert-True ($sources -match 'last_audio_priority \? last_audio_priority : 1') 'Recuperacao da mesma reproducao perdeu prioridade sobre a preferencia geral.'
Assert-True ($sources -match 'player_select_hls_streams\(fmt, vidx, aidx') 'A reabertura nao descarta faixas HLS fora de uso.'
Assert-True ($sources -match 'avformat_seek_file' -and $sources -match 'window-fallback') 'Seek perdeu a janela multi-stream ou o fallback compativel.'
Assert-True ($sources -match 'demux_worker_thread' -and $sources -match 'DEMUX_QUEUE_BYTES') 'Rede/demux voltou a bloquear a thread dos controles.'
Assert-True ($sources -match 'pause_request' -and $sources -match 'demux_worker_wait_paused') 'Seek/troca de faixa perdeu a barreira da thread de demux.'
Assert-True ($sources -match 'demux_worker_clear' -and $sources -match 'player_seek_with_barrier') 'Seek pode misturar pacotes anteriores com a nova geracao.'
Assert-True ($sources -match 'if \(!on_render_thread\) return 0') 'Callback do FFmpeg voltou a ler estado nao atomico na thread de demux.'
Assert-True ($sources -match 'loading_owner = PLAYER_LOADING_PLAYBACK' -and $sources -match 'player_loading_interrupt_can_draw') 'Loader de abertura pode voltar a disputar a tela com buffering/video.'
Assert-True ($sources -match 'audio-inplace-applied' -and $sources -match 'subtitle-inplace-applied') 'Troca de faixa voltou a reabrir toda a sessao HLS.'
Assert-True ($sources -match 'inplace-resume-timeout' -and $sources -match 'PLAYER_RESTART_TRACK') 'Operacao interna pode voltar a prender o player sem fallback.'
Assert-True ($sources -match 'subtitle_hint_priority' -and $sources -match 'last_audio_priority = 2') 'Reabertura nao preserva exatamente as faixas escolhidas.'
Assert-True ($sources -match 'nplay_curl_avio_hls_media_counts' -and $sources -match 'master_subtitle_count == 0') 'Legendas anunciadas no master podem voltar a ser descartadas antes do probe.'
Assert-True ($sources -match 'subtitle_cue_times' -and $sources -match 'pkt_timebase') 'Timestamp/tempo-base das legendas voltou a ficar implicito.'
Assert-True ($sources -match 'stream_track_title' -and $sources -match '"comment"') 'Nome LANGUAGE/NAME das renditions HLS voltou a ser ignorado.'
Assert-True ($sources -match 'cid == AV_CODEC_ID_NONE' -and $sources -match 'AV_CODEC_ID_WEBVTT' -and $sources -match 'repaired_subtitles') 'Legendas WebVTT sem probe completo podem voltar a desaparecer do painel.'
$subtitleHeader = Get-Content include/subtitle_queue.h -Raw
Assert-True ($subtitleHeader -match 'SUBTITLE_QUEUE_CAP 32') 'Fila de legendas voltou a descartar cues cedo demais.'
Assert-True ($sources -match 'left > 8 \? 8 : left') 'Espera de video voltou a bloquear comandos por centenas de milissegundos.'
Assert-True ($sources -match 'EXIT_REASON_NEXT_EPISODE' -and $sources -match 'PLAYER_REQUEST_NEXT') 'HUD anuncia proximo episodio sem entregar a acao ao fluxo da serie.'
Assert-True ($sources -match 'next_selected' -and $sources -match 'next-episode-touch') 'Proximo episodio perdeu confirmacao pelo controle ou toque.'

$diagSource = Get-Content source/diag.c -Raw
Assert-True ($diagSource -match 'sdmc:/switch/\.nplay-player-trace\.log') 'Trace detalhado do player nao persiste na raiz de switch.'
Assert-True ($diagSource -match 'InfoType_UsedMemorySize') 'Trace nao registra a reserva de memoria do processo.'
Assert-True ($diagSource -match 'mallinfo') 'Trace nao registra uso e folga reais do heap.'
Assert-True ($diagSource -match 'safe_path') 'Trace de rede pode voltar a persistir query string privada.'
Assert-True ($diagSource -notmatch 'play_url|Authorization|Bearer') 'Trace diagnostico contem campo sensivel.'

$mainSource = Get-Content source/main.c -Raw
Assert-True ($mainSource -match 'SDL_CreateThread\(landing_fetch_thread') 'Catalogo voltou a bloquear a thread de interface.'
Assert-True ($mainSource -match 'g_land_cache\[5\]') 'Troca de aba perdeu o cache de catalogo.'
Assert-True ($mainSource -match 'api_get_timeout\(landing_path\(tab\), 6L, 30L\)') 'Series voltou ao timeout curto ou sincrono.'
Assert-True ($mainSource -notmatch 'prefetch_order\[\] = \{ 1, 2, 3, 4 \}') 'Catalogos voltaram a ocupar heap automaticamente antes do player.'
Assert-True ($mainSource -match 'playback_memory_enter') 'Player nao reserva memoria antes de abrir HLS.'
Assert-True ($mainSource -match 'hero_pool_add_ready\(g_heroesArr, cJSON_GetObjectItem\(g_land, "featured"\)\)' -and $mainSource -match 'jstr\(item, "hero_type"\)') 'Destaques do Switch perderam tipo ou curadoria pronta do site.'
Assert-True ($mainSource -match 'cover_suspend_and_release') 'Workers de capa podem voltar a competir com a abertura HLS.'
Assert-True ($mainSource -match 'load_player_boot_stage') 'A ultima etapa antes de um crash nao aparece no diagnostico.'
Assert-True ($mainSource -match 'diag_read_player_page') 'Tela de diagnostico nao mostra o trace preservado apos crash.'
Assert-True ($mainSource -match 'diag_read_network_tail') 'Tela de diagnostico nao mostra latencia das requisicoes.'
Assert-True ($mainSource -match 'req\.audio_pref = g_next_audio_pref_override >= 0 \? g_next_audio_pref_override : g_pref_audio') 'Player nao recebe a preferencia da versao/conta.'
Assert-True ($mainSource -match 'audio_effective_preference\(' -and $mainSource -match 'g_series_audio_explicit') 'Variante-base pode voltar a sobrescrever a preferencia de audio da conta.'
Assert-True ($mainSource -match 'req\.container = is_hls \|\| url_hls \? "m3u8" : NULL') 'Fluxo preparado voltou a ignorar que a URL e HLS.'
Assert-True ($mainSource -match 'req\.delivery = DELIVERY_R2') 'Fluxo HLS preparado nao recebe o contrato R2 de codecs/legendas.'
Assert-True ($mainSource -match 'g_next_audio_hint = audio_hint') 'Episodio seguinte nao preserva a faixa de audio anterior.'
Assert-True ($mainSource -match 'g_next_audio_language') 'Episodio seguinte preserva apenas indice e pode mudar para ingles.'
Assert-True ($mainSource -match 'series_keep_audio_after_switch') 'Temporada agrupada nao tenta preservar sua versao de audio.'
Assert-True ($mainSource -match 'play_episode_sequence\([^\)]*cJSON \*episode_hint' -and $mainSource -match 'src\.season > 0 \|\| src\.episode > 0') 'HUD de episodio pode voltar a perder temporada e episodio em acessos diretos.'

$storeSource = Get-Content source/store.c -Raw
Assert-True ($storeSource -match 'pref_audio_%d\.txt' -and $storeSource -match 'pref_sub_%d\.txt') 'Preferencias manuais de audio/legenda vazam entre perfis.'

$playerUiSource = Get-Content source/player_ui.c -Raw
Assert-True ($playerUiSource -match 'draw_pause_info' -and $playerUiSource -match 'draw_panel') 'HUD modular perdeu pausa detalhada ou painel de faixas.'
Assert-True ($playerUiSource -match 'ui_popcorn_draw') 'Tela de preparacao perdeu a animacao de pipoca do Nplay.'
Assert-True ($playerUiSource -match 'draw_track_column.+AUDIO' -or ($playerUiSource -match '"AUDIO"' -and $playerUiSource -match '"LEGENDAS"')) 'Painel nao mostra audio e legendas em duas colunas.'
Assert-True ($playerUiSource -match 'draw_next_card' -and $playerUiSource -match 'PUI_FOCUS_TIMELINE') 'HUD modular perdeu proximo episodio ou timeline.'
Assert-True ($playerUiSource -match 'ui_popcorn_draw') 'Tela de preparacao perdeu a animacao de pipoca do Nplay.'
Assert-True ($sources -match 'PlayerHud hud_base' -and $sources -match 'draw_hud\(ren, &hud_base') 'HUD voltou a enumerar faixas ou montar rotulos a cada quadro.'

$audioPolicySource = Get-Content source/audio_policy.c -Raw
Assert-True ($audioPolicySource -match 'count == 2' -and $audioPolicySource -match 'AUDIO_KIND_ENGLISH' -and $audioPolicySource -match 'AUDIO_KIND_UNKNOWN') 'Pacotes R2 antigos (ingles + dublagem sem tag) voltaram a selecionar ingles.'
Assert-True ($audioPolicySource -match 'continuity_priority' -and $audioPolicySource -match 'outro episodio') 'Pista de outro episodio pode voltar a vencer PT-BR em Dublado.'

$apiSource = Get-Content source/api.c -Raw
Assert-True ($apiSource -match '/api/stream/session/%d/refresh') 'Refresh da mesma sessao nao esta implementado.'
Assert-True ($apiSource -match '/api/stream/session/%d/fail') 'Failover para outra fonte nao esta implementado.'
Assert-True ($apiSource -match '/api/stream/session/%d/heartbeat') 'Heartbeat da sessao nao esta implementado.'
Assert-True ($apiSource -match '/api/sync/progress') 'Progresso periodico nao esta implementado.'
Assert-True ($apiSource -match 'api_reresolve_playback') 'Nova resolucao curta para recuperacao nao esta implementada.'
Assert-True ($apiSource -match 'diag_network_event') 'Chamadas da API nao registram codigo e duracao para diagnostico.'

$tlsPatch = Get-Content tools/ffmpeg-libnx-tls-hostname.patch -Raw
$ffmpegBuild = Get-Content tools/build_ffmpeg_https.sh -Raw
Assert-True ($tlsPatch -match 'SslVerifyOption_PeerCa \| SslVerifyOption_HostName') 'Patch TLS nao valida CA e hostname juntos.'
Assert-True ($ffmpegBuild -match 'ffmpeg-libnx-tls-hostname\.patch') 'Build do FFmpeg nao aplica o patch TLS local.'

$caBundle = Join-Path $root 'data/cacert.bin'
Assert-True (Test-Path $caBundle) 'Bundle CA Mozilla nao foi incluido no NRO.'
$caHash = (Get-FileHash $caBundle -Algorithm SHA256).Hash.ToLowerInvariant()
Assert-True ($caHash -eq 'f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9') 'Bundle CA diverge do checksum oficial do curl.'

& (Join-Path $PSScriptRoot 'validate_site_contract.ps1')

if (-not $SkipBuild) {
    & make clean
    if ($LASTEXITCODE -ne 0) { throw 'make clean falhou.' }
    & make -j4
    if ($LASTEXITCODE -ne 0) { throw 'make -j4 falhou.' }
}

Assert-True (Test-Path Nplay.nro) 'Nplay.nro nao foi gerado.'
Assert-True (Test-Path Nplay.elf) 'Nplay.elf nao foi gerado.'
$hostGcc = 'C:\devkitPro\msys2\usr\bin\gcc.exe'
Assert-True (Test-Path $hostGcc) 'GCC host nao encontrado para as simulacoes.'
& $hostGcc -std=c11 -Wall -Wextra -Iinclude source/genre_label.c tools/test_genre_label.c -o build/test_genre_label.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao de rotulos falhou ao compilar.' }
& .\build\test_genre_label.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao de rotulos falhou.' }
& $hostGcc -std=c11 -Wall -Wextra -Iinclude source/player_clock.c tools/test_player_clock.c -lm -o build/test_player_clock.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao do relogio do player falhou ao compilar.' }
& .\build\test_player_clock.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao do relogio do player falhou.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/player_next.c tools/test_player_next.c -lm -o build/test_player_next.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao do cartao de proximo episodio falhou ao compilar.' }
& .\build\test_player_next.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao do cartao de proximo episodio falhou.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/player_loading.c tools/test_player_loading.c -o build/test_player_loading.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao da propriedade da tela de carregamento falhou ao compilar.' }
& .\build\test_player_loading.exe
if ($LASTEXITCODE -ne 0) { throw 'Loader de abertura voltou a disputar a tela com o buffering.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/audio_policy.c tools/test_audio_policy.c -o build/test_audio_policy.exe
if ($LASTEXITCODE -ne 0) { throw 'Politica de audio falhou ao compilar.' }
& .\build\test_audio_policy.exe
if ($LASTEXITCODE -ne 0) { throw 'Politica de audio falhou nos cenarios HLS/continuidade.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/hls_manifest.c tools/test_hls_manifest.c -o build/test_hls_manifest.exe
if ($LASTEXITCODE -ne 0) { throw 'Parser do manifesto HLS falhou ao compilar.' }
& .\build\test_hls_manifest.exe
if ($LASTEXITCODE -ne 0) { throw 'Parser do manifesto HLS falhou ao detectar audio/legendas.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/subtitle_queue.c tools/test_subtitle_queue.c -lm -o build/test_subtitle_queue.exe
if ($LASTEXITCODE -ne 0) { throw 'Fila de legendas falhou ao compilar.' }
& .\build\test_subtitle_queue.exe
if ($LASTEXITCODE -ne 0) { throw 'Fila de legendas falhou nos cenarios de tempo e sobreposicao.' }
& $hostGcc -std=c11 -Wall -Wextra -ffunction-sections -fdata-sections '-Wl,--gc-sections' -Itools/host-stubs -Iinclude source/api.c source/cJSON.c tools/test_hot_stream_api.c -lm -o build/test_hot_stream_api.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao TorBox/R2 falhou ao compilar.' }
& .\build\test_hot_stream_api.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao TorBox/R2 falhou.' }
& $hostGcc -std=c11 -Wall -Wextra -Iinclude source/episode_flow.c source/cJSON.c tools/test_episode_flow.c -lm -o build/test_episode_flow.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao de episodios falhou ao compilar.' }
& .\build\test_episode_flow.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao de episodios falhou.' }
if ((Get-Command ffmpeg -ErrorAction SilentlyContinue) -and
    (Get-Command ffprobe -ErrorAction SilentlyContinue)) {
    & node tools/test_chunked_remux.mjs
    if ($LASTEXITCODE -ne 0) { throw 'Simulacao local do remux chunked falhou.' }
    & node tools/test_hls_player_fixture.mjs
    if ($LASTEXITCODE -ne 0) { throw 'Fixture HLS multifaixa/seek/legendas falhou.' }
}
$nm = 'C:\devkitPro\devkitA64\bin\aarch64-none-elf-nm.exe'
Assert-True (Test-Path $nm) 'aarch64-none-elf-nm nao encontrado.'
$symbols = (& $nm Nplay.elf) -join "`n"
foreach ($symbol in @('ff_https_protocol','ff_hls_demuxer','ff_h264_nvtegra_hwaccel','av_hwdevice_ctx_create')) {
    Assert-True ($symbols -match [regex]::Escape($symbol)) "Simbolo obrigatorio ausente: $symbol"
}

$hash = (Get-FileHash Nplay.nro -Algorithm SHA256).Hash.ToLowerInvariant()
$size = (Get-Item Nplay.nro).Length
Write-Host "OK Nplay $makeVersion | $size bytes | sha256:$hash"
