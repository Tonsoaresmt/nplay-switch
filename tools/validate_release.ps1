param([switch]$SkipBuild, [switch]$SkipMediaFixtures)

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
Assert-True ($sources -notmatch 'CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT') 'Pool de conexoes libcurl compartilhado entre threads concorrentes.'
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
Assert-True ($sources -match 'player_sync_exit_should_cancel' -and $sources -match 'cancel_io') 'Saida do player pode voltar a aguardar o timeout integral da rede.'
Assert-True ($sources -notmatch 'Save progress once at the end') 'Progresso final voltou a ser salvo de forma sincrona na thread da interface.'
Assert-True ($sources -match 'retry_limit = startup_failure \? 2 : 3') 'Falha inicial voltou a encerrar antes de tentar a fonte alternativa.'
Assert-True ($sources -match 'player_recovery_thread' -and $sources -match 'player_recovery_call') 'Renovacao de sessao voltou a bloquear a interface do player.'
Assert-True ($sources -match 'player_boot_stage\("03 abrindo fonte"\)') 'Crash do player voltou a nao deixar diagnostico persistente.'
Assert-True ($sources -match 'sdmc:/switch/\.nplay-player-boot\.txt') 'Diagnostico de crash depende de uma subpasta opcional.'
Assert-True ($sources -match 'diag_player_begin') 'Trace persistente nao e iniciado para cada reproducao.'
Assert-True ($sources -match 'first-frame') 'Trace nao distingue falha anterior ao primeiro frame.'
Assert-True ($sources -match 'first-present') 'Trace nao confirma a primeira apresentacao no renderer.'
Assert-True ($sources -match 'lang_norm' -and $sources -match 'stream_norm') 'Selecao de idioma voltou a comparar tags inconsistentes diretamente.'
Assert-True ($sources -match 'audio_policy_choose') 'Selecao de audio voltou a ficar acoplada ao player e sem testes.'
Assert-True ($sources -match 'audio_pref_explicit' -and $sources -match 'saved_audio') 'Contexto explicito de audio nao chega ao player.'
$audioPolicySource = Get-Content source/audio_policy.c -Raw
Assert-True ($audioPolicySource -match 'account_pref == 2 && saved\[0\]') 'Idioma antigo salvo pode voltar a sobrepor Dublado/Legendado da conta.'
Assert-True ($sources -match 'pui_draw\(' -and $sources -match 'pui_draw_loading\(') 'Player nao usa o HUD modular nas telas de reproducao e abertura.'
Assert-True ($sources -match 'attempt\.audio_hint = last_audio') 'Recuperacao de sessao nao preserva a faixa de audio.'
Assert-True ($sources -match 'attempt\.audio_hint_language = last_audio_language') 'Recuperacao preserva indice, mas pode trocar de idioma.'
Assert-True ($sources -match 'last_audio_priority \? last_audio_priority : 1') 'Recuperacao da mesma reproducao perdeu prioridade sobre a preferencia geral.'
Assert-True ($sources -match 'player_select_hls_streams\(fmt, vidx, aidx') 'A reabertura nao descarta faixas HLS fora de uso.'
Assert-True ($sources -match 'avformat_seek_file' -and $sources -match 'window-fallback') 'Seek perdeu a janela multi-stream ou o fallback compativel.'
Assert-True ($sources -match 'demux_worker_thread' -and $sources -match 'DEMUX_QUEUE_BYTES') 'Rede/demux voltou a bloquear a thread dos controles.'
Assert-True ($sources -match 'pause_request' -and $sources -match 'demux_worker_wait_paused') 'Seek/troca de faixa perdeu a barreira da thread de demux.'
Assert-True ($sources -match 'demux_worker_clear' -and $sources -match 'player_seek_with_barrier') 'Seek pode misturar pacotes anteriores com a nova geracao.'
Assert-True ($sources -match 'hls_media_playlist_trim' -and $sources -match 'positioned-reopen' -and $sources -match 'nplay_curl_avio_set_hls_start\(start_sec\)') 'Seek/retomada HLS voltou ao seek interno do FFmpeg 7.1, que corrompe renditions fMP4.'
Assert-True ($sources -match 'meta_cache_get' -and $sources -match 'hls_is_init_section_url') 'Reabertura posicionada voltou a baixar playlists e init a cada salto.'
Assert-True ($sources -match 'quick_seek_deadline' -and $sources -notmatch 'A confirma  \|  B cancela  \|  L/R ajusta') 'L/R/ZL/ZR voltaram a congelar o video esperando A.'
Assert-True ($sources -match 'DEMUX_QUEUE_BYTES_SEQUENTIAL' -and $sources -match 'demux_worker_take_stream') 'Remux sequencial voltou a engasgar o audio atras de blocos de video.'
Assert-True ($sources -match 'sequential-duration' -and $sources -match 'hot_probe_duration') 'Remux sequencial voltou a usar a duracao do primeiro fragmento (episodio marcado como visto).'
Assert-True ($sources -match 'track_title_is_technical') 'Painel de audio voltou a mostrar nomes tecnicos como audio_0.'
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
Assert-True ($mainSource -match 'ui_header_action_hit\(x, y\)' -and $mainSource -notmatch 'y < 95 && x < 260') 'Voltar por touch nao coincide com a acao desenhada no cabecalho.'
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
Assert-True ($mainSource -match 'g_avatar_lookup\[512\]' -and $mainSource -match 'profile_avatar_key_url') 'Avatares voltaram a varrer todo o catalogo em cada quadro.'
Assert-True ($mainSource -match 'dicebear\.com/9\.x/%\.\*s/png\?seed=%s&size=256') 'Avatares DiceBear deixaram de solicitar uma imagem raster compativel com o Switch.'
Assert-True ($mainSource -match 'AVATAR_PICKER_COLS 5' -and $mainSource -match 'avatar_size = selected \? 184 : 164') 'Seletor de perfis perdeu o destaque visual da foto selecionada.'
Assert-True ($mainSource -match 'g_screen == SC_PROFILES \? 0u : 1200u' -and $mainSource -match 'g_screen == SC_PROFILES && !g_avatar_catalog') 'Catalogo de fotos pode voltar a iniciar tarde demais no seletor de perfis.'
Assert-True ($mainSource -match 'draw_profile_avatar_style\(active, 1184, 12, 70' -and $mainSource -match 'profile_status = \{ 1237, 65, 16, 16 \}') 'Avatar do cabecalho voltou a ficar pequeno ou sem destaque de sessao.'
Assert-True ($mainSource -match 'draw_profile_avatar\(active, 856, 99, 86\)') 'Menu rapido voltou a esconder a foto do perfil ativo.'
Assert-True ($mainSource -match 'SDL_HINT_TOUCH_MOUSE_EVENTS' -and $mainSource -match 'SDL_IGNORE') 'Touch pode voltar a gerar uma segunda corrente sintetica de mouse.'
Assert-True ($mainSource -match 'SDL_FINGERMOTION' -and $mainSource -match 'touch_input_move') 'Arrastar voltou a ser interpretado somente no momento em que o dedo solta.'
Assert-True ($mainSource -match 'touch_scroll_apply' -and $mainSource -match 'touch_momentum_update') 'Rolagem direta perdeu deslocamento continuo ou inercia.'
Assert-True ($mainSource -notmatch 'handle_touch_swipe') 'Gestos voltaram a simular passos do direcional.'
Assert-True ($mainSource -match 'TOUCH_SURFACE_MOVIE_RELATED' -and $mainSource -match 'TOUCH_SURFACE_AVATAR_PAGES') 'Relacionados ou seletor de fotos perderam navegacao tactil direta.'
Assert-True ($sources -match 'touch_track_button' -and $sources -match 'touched_menu') 'Painel de audio e legendas voltou a ignorar toque direto.'
Assert-True ($mainSource -match '/api/device/code' -and $mainSource -match '/api/device/token') 'Entrada por QR perdeu criacao ou polling do codigo.'
Assert-True ($mainSource -match 'SDL_CreateThread\(login_pairing_thread') 'Pareamento voltou a bloquear a thread grafica.'
$pairingSource = Get-Content source/device_pairing.c -Raw
Assert-True ($pairingSource -match 'DEVICE_PAIRING_POLL_SLOW_DOWN' -and $pairingSource -match '\*interval_seconds \+= 5') 'Polling nao respeita slow_down do fluxo de dispositivo.'
Assert-True ($pairingSource -match 'bit-rows-v1' -and $pairingSource -match 'DEVICE_PAIRING_QR_MAX') 'QR nativo perdeu validacao de formato ou limite de memoria.'

$uiSource = Get-Content source/ui.c -Raw
Assert-True ($uiSource -match 'void ui_avatar' -and $uiSource -match 'SDL_RenderGeometry') 'Avatares circulares deixaram de usar recorte acelerado pela GPU.'
Assert-True ($uiSource -match 'static float unit_x' -and $uiSource -match 'unit_ready') 'Geometria circular dos avatares voltou a recalcular trigonometria por quadro.'

$storeSource = Get-Content source/store.c -Raw
Assert-True ($storeSource -match 'pref_audio_%d\.txt' -and $storeSource -match 'pref_sub_%d\.txt') 'Preferencias manuais de audio/legenda vazam entre perfis.'

$playerUiSource = Get-Content source/player_ui.c -Raw
Assert-True ($playerUiSource -match 'draw_pause_info' -and $playerUiSource -match 'draw_panel') 'HUD modular perdeu pausa detalhada ou painel de faixas.'
Assert-True ($playerUiSource -match 'ui_popcorn_draw') 'Tela de preparacao perdeu a animacao de pipoca do Nplay.'
Assert-True ($playerUiSource -match 'draw_episodes' -and $playerUiSource -match 'pui_set_loading_backdrop') 'Player perdeu o painel Episodios ou o ultimo quadro durante seek.'
Assert-True ($playerUiSource -match 'h->subtitle_text, ST_SUB, PUI_W - 200, 4\)') 'Duas falas simultaneas voltaram a ser cortadas em duas linhas.'
Assert-True ($playerUiSource -match 'draw_track_column.+AUDIO' -or ($playerUiSource -match '"AUDIO"' -and $playerUiSource -match '"LEGENDAS"')) 'Painel nao mostra audio e legendas em duas colunas.'
Assert-True ($playerUiSource -match 'draw_next_card' -and $playerUiSource -match 'PUI_FOCUS_TIMELINE') 'HUD modular perdeu proximo episodio ou timeline.'
Assert-True ($playerUiSource -match 'ui_popcorn_draw') 'Tela de preparacao perdeu a animacao de pipoca do Nplay.'
Assert-True ($playerUiSource -match 'draw_episodes' -and $playerUiSource -match 'pui_set_loading_backdrop') 'Player perdeu o painel Episodios ou o ultimo quadro durante seek.'
Assert-True ($playerUiSource -match 'h->subtitle_text, ST_SUB, PUI_W - 200, 4\)') 'Duas falas simultaneas voltaram a ser cortadas em duas linhas.'
Assert-True ($sources -match 'PlayerHud hud_base' -and $sources -match 'draw_hud\(ren, &hud_base') 'HUD voltou a enumerar faixas ou montar rotulos a cada quadro.'

$audioPolicySource = Get-Content source/audio_policy.c -Raw
Assert-True ($audioPolicySource -match 'count == 2' -and $audioPolicySource -match 'AUDIO_KIND_ENGLISH' -and $audioPolicySource -match 'AUDIO_KIND_UNKNOWN') 'Pacotes R2 antigos (ingles + dublagem sem tag) voltaram a selecionar ingles.'
Assert-True ($audioPolicySource -match 'continuity_priority' -and $audioPolicySource -match 'outro episodio') 'Pista de outro episodio pode voltar a vencer PT-BR em Dublado.'

$apiSource = Get-Content source/api.c -Raw
Assert-True ($apiSource -match 'hot_session_id' -and $apiSource -match 'hot_subtitle_session_valid') 'ID textual da sessao remux foi perdido ou confundido com a sessao de heartbeat.'
$hotSubtitleSource = Get-Content source/hot_subtitles.c -Raw
Assert-True ($hotSubtitleSource -match 'https://' -and $hotSubtitleSource -match 'index > 100' -and $hotSubtitleSource -match 'HLS_MANIFEST_TRACK_CAP') 'Resolver de legendas remux perdeu restricao de origem, indice ou teto de faixas.'
$vttSource = Get-Content source/vtt_stream.c -Raw
Assert-True ($vttSource -match 'VTT_BLOCK_LIMIT' -and $vttSource -match 'WEBVTT' -and $vttSource -match 'skip_lf') 'Parser WebVTT progressivo perdeu limite, cabecalho ou compatibilidade CRLF.'
Assert-True ($sources -match 'net_stream_text' -and $sources -match 'progressive_subtitle_stop' -and $sources -match 'external_subtitle_clear\(store\); store->progressive = stream') 'Legenda remux deixou de usar transporte isolado, cancelamento proprio ou aplicacao transacional.'
Assert-True ($mainSource -notmatch '\bapi_send\(' -and $mainSource -notmatch 'api_get_timeout\(p, 2L, 5L\)') 'Operacoes de conta/progresso voltaram a bloquear a UI.'
$uiRequestSource = Get-Content source/ui_request.c -Raw
Assert-True ($uiRequestSource -match 'SDL_CreateThread' -and $uiRequestSource -match 'SDL_WaitThread' -and $uiRequestSource -match 'SDL_FINGERDOWN') 'Modal de rede perdeu worker, ownership ou cancelamento touch.'
Assert-True ($apiSource -match 'api_refresh_playback_cancel' -and $apiSource -match 'api_fail_playback_cancel') 'Recuperacao de sessao nao pode ser cancelada por B.'
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
$hostGcc = $env:HOST_CC
if (-not $hostGcc) {
    $gccCommand = Get-Command gcc -ErrorAction SilentlyContinue
    if ($gccCommand) { $hostGcc = $gccCommand.Source }
    elseif ($env:DEVKITPRO) { $hostGcc = Join-Path $env:DEVKITPRO 'msys2/usr/bin/gcc.exe' }
}
Assert-True (Test-Path $hostGcc) 'GCC host nao encontrado para as simulacoes.'
$env:HOST_CC = $hostGcc
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude tools/test_player_buffer.c -o build/test_player_buffer.exe
if ($LASTEXITCODE -ne 0) { throw 'Politica de buffer falhou ao compilar.' }
& .\build\test_player_buffer.exe
if ($LASTEXITCODE -ne 0) { throw 'Politica de buffer falhou.' }
& node tools/test_demux_worker.mjs
if ($LASTEXITCODE -ne 0) { throw 'Concorrencia do worker demux falhou.' }
& node tools/test_curl_avio_wait.mjs
if ($LASTEXITCODE -ne 0) { throw 'AVIO de rede falhou.' }
& node tools/test_player_supervisor.mjs
if ($LASTEXITCODE -ne 0) { throw 'Supervisor perdeu ponto salvo.' }
& node tools/test_seek_barrier.mjs
if ($LASTEXITCODE -ne 0) { throw 'Barreira de seek reabriu fonte ocupada.' }
& node tools/test_curl_backpressure_probe.mjs
if ($LASTEXITCODE -ne 0) { throw 'libcurl real falhou em backpressure ou idle.' }
& node tools/test_subtitle_io.mjs
if ($LASTEXITCODE -ne 0) { throw 'Isolamento de legendas remux falhou.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/vtt_stream.c tools/test_vtt_stream.c -o build/test_vtt_stream.exe
if ($LASTEXITCODE -ne 0) { throw 'Parser progressivo WebVTT falhou ao compilar.' }
& .\build\test_vtt_stream.exe
if ($LASTEXITCODE -ne 0) { throw 'Parser progressivo WebVTT falhou.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Itools/host-stubs -Iinclude source/ui_request.c source/cJSON.c tools/test_ui_request.c -lm -o build/test_ui_request.exe
if ($LASTEXITCODE -ne 0) { throw 'Modal de rede falhou ao compilar.' }
& .\build\test_ui_request.exe
if ($LASTEXITCODE -ne 0) { throw 'Modal de rede falhou.' }
& $hostGcc -std=c11 -Wall -Wextra -Iinclude source/genre_label.c tools/test_genre_label.c -o build/test_genre_label.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao de rotulos falhou ao compilar.' }
& .\build\test_genre_label.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao de rotulos falhou.' }
& $hostGcc -std=c11 -Wall -Wextra -Iinclude source/player_clock.c tools/test_player_clock.c -lm -o build/test_player_clock.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao do relogio do player falhou ao compilar.' }
& .\build\test_player_clock.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao do relogio do player falhou.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/player_sync.c tools/test_player_sync.c -o build/test_player_sync.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao de encerramento/sincronizacao falhou ao compilar.' }
& .\build\test_player_sync.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao de encerramento/sincronizacao falhou.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/player_next.c tools/test_player_next.c -lm -o build/test_player_next.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao do cartao de proximo episodio falhou ao compilar.' }
& .\build\test_player_next.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao do cartao de proximo episodio falhou.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/player_loading.c tools/test_player_loading.c -o build/test_player_loading.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao da propriedade da tela de carregamento falhou ao compilar.' }
& .\build\test_player_loading.exe
if ($LASTEXITCODE -ne 0) { throw 'Loader de abertura voltou a disputar a tela com o buffering.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/touch_input.c tools/test_touch_input.c -o build/test_touch_input.exe
if ($LASTEXITCODE -ne 0) { throw 'Reconhecedor de toque falhou ao compilar.' }
& .\build\test_touch_input.exe
if ($LASTEXITCODE -ne 0) { throw 'Reconhecedor de toque falhou em tap, eixo, arraste ou ownership.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/audio_policy.c tools/test_audio_policy.c -o build/test_audio_policy.exe
if ($LASTEXITCODE -ne 0) { throw 'Politica de audio falhou ao compilar.' }
& .\build\test_audio_policy.exe

& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude tools/test_player_recovery.c -o build/test_player_recovery.exe
& .\build\test_player_recovery.exe
if ($LASTEXITCODE -ne 0) { throw 'Politica de audio falhou nos cenarios HLS/continuidade.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/hls_manifest.c tools/test_hls_manifest.c -o build/test_hls_manifest.exe
if ($LASTEXITCODE -ne 0) { throw 'Parser do manifesto HLS falhou ao compilar.' }
& .\build\test_hls_manifest.exe
if ($LASTEXITCODE -ne 0) { throw 'Parser do manifesto HLS falhou ao detectar audio/legendas.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/subtitle_queue.c tools/test_subtitle_queue.c -lm -o build/test_subtitle_queue.exe
if ($LASTEXITCODE -ne 0) { throw 'Fila de legendas falhou ao compilar.' }
& .\build\test_subtitle_queue.exe
if ($LASTEXITCODE -ne 0) { throw 'Fila de legendas falhou nos cenarios de tempo e sobreposicao.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/hot_subtitles.c source/cJSON.c tools/test_hot_subtitles.c -lm -o build/test_hot_subtitles.exe
Assert-True ($LASTEXITCODE -eq 0) 'Compilacao hot subtitles falhou.'
& .\build\test_hot_subtitles.exe
Assert-True ($LASTEXITCODE -eq 0) 'Contrato hot subtitles falhou.'
& $hostGcc -std=c11 -Wall -Wextra -ffunction-sections -fdata-sections '-Wl,--gc-sections' -Itools/host-stubs -Iinclude source/api.c source/hot_subtitles.c source/cJSON.c tools/test_hot_stream_api.c -lm -o build/test_hot_stream_api.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao TorBox/R2 falhou ao compilar.' }
& .\build\test_hot_stream_api.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao TorBox/R2 falhou.' }
& $hostGcc -std=c11 -Wall -Wextra -Iinclude source/episode_flow.c source/cJSON.c tools/test_episode_flow.c -lm -o build/test_episode_flow.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao de episodios falhou ao compilar.' }
& .\build\test_episode_flow.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao de episodios falhou.' }
& node tools/test_episode_sequence.mjs
if ($LASTEXITCODE -ne 0) { throw 'Sequencia real de episodios perdeu contexto ou proximo episodio.' }
Assert-True ($sources -match 'next_selected \|\| paused \|\| hud_pinned') 'Proximo episodio voltou a ficar escondido durante a pausa.'
Assert-True ($mainSource -match 'play_with_progress_details' -and $mainSource -match 'presentation->has_next' -and $mainSource -match 'g_episode_pending\.explicit_next') 'Preparacao ou virada de temporada perdeu a acao explicita de proximo episodio.'
Assert-True ($mainSource -match 'play_episodes_build' -and $mainSource -match 'chosen_item > 0 \? chosen_item') 'Painel Episodios do player nao toca mais o episodio escolhido.'
Assert-True ($mainSource -match 'g_playback_chain > 0\) return' -and $mainSource -match 'play_episode_sequence_run') 'Home voltou a recarregar entre episodios encadeados.'
Assert-True ($mainSource -match 'resolve_progress_thread' -and $mainSource -match 'progress_fetched') 'Progresso salvo voltou a ser pedido so depois de /stream.'
Assert-True ($sources -match 'nplay_curl_avio_hls_prefetch\(url,' -and $sources -match 'HLS_PREFETCH_WAVE') 'Playlists/init HLS voltaram a ser baixadas uma a uma na abertura.'
Assert-True ($sources -match 'hls_manifest_resolve_like_ffmpeg') 'Busca paralela deve resolver URLs como o FFmpeg (sem herdar query).'
Assert-True ($sources -match 'player_hls_choose_audio' -and $sources -match 'hls_manifest_keep_audio' -and $sources -match 'audio_from_master') 'HLS voltou a abrir todas as faixas de audio.'
Assert-True ($sources -match 'subtitle_fetch_start' -and $sources -match 'subtitle_fetch_stop\(&subtitle_fetch\)') 'Legenda do master voltou a bloquear abertura/troca.'
Assert-True ($sources -match 't_cancel_flag') 'Download de legenda em segundo plano perdeu o cancelamento.'
& $hostGcc -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections '-Wl,--gc-sections' -Itools/host-stubs -Iinclude source/api.c source/hot_subtitles.c source/cJSON.c tools/test_playback_source.c -lm -o build/test_playback_source.exe
if ($LASTEXITCODE -ne 0) { throw 'Contrato anime/R2 falhou ao compilar.' }
& .\build\test_playback_source.exe
if ($LASTEXITCODE -ne 0) { throw 'Contrato anime/R2 perdeu a fonte escolhida pelo backend.' }
& $hostGcc -std=c11 -Wall -Wextra -Werror -Iinclude source/device_pairing.c source/cJSON.c tools/test_device_pairing.c -lm -o build/test_device_pairing.exe
if ($LASTEXITCODE -ne 0) { throw 'Simulacao do pareamento QR falhou ao compilar.' }
& .\build\test_device_pairing.exe
if ($LASTEXITCODE -ne 0) { throw 'Pareamento QR falhou em codigo, matriz, token ou backoff.' }
if (-not $SkipMediaFixtures) {
    Assert-True ([bool](Get-Command ffmpeg -ErrorAction SilentlyContinue)) 'FFmpeg necessario para validacao completa; use -SkipMediaFixtures apenas para verificacao parcial.'
    Assert-True ([bool](Get-Command ffprobe -ErrorAction SilentlyContinue)) 'FFprobe necessario para validacao completa.'
}
if (-not $SkipMediaFixtures -and (Get-Command ffmpeg -ErrorAction SilentlyContinue) -and
    (Get-Command ffprobe -ErrorAction SilentlyContinue)) {
    & node tools/test_chunked_remux.mjs
    if ($LASTEXITCODE -ne 0) { throw 'Simulacao local do remux chunked falhou.' }
    & node tools/test_hls_player_fixture.mjs
    if ($LASTEXITCODE -ne 0) { throw 'Fixture HLS multifaixa/seek/legendas falhou.' }
}
$nm = $env:TARGET_NM
if (-not $nm) {
    $nmCommand = Get-Command aarch64-none-elf-nm -ErrorAction SilentlyContinue
    if ($nmCommand) { $nm = $nmCommand.Source }
    elseif ($env:DEVKITA64) { $nm = Join-Path $env:DEVKITA64 'bin/aarch64-none-elf-nm' }
}
Assert-True (Test-Path $nm) 'aarch64-none-elf-nm nao encontrado.'
$symbols = (& $nm Nplay.elf) -join "`n"
foreach ($symbol in @('ff_https_protocol','ff_hls_demuxer','ff_webvtt_demuxer','ff_webvtt_decoder','ff_h264_nvtegra_hwaccel','av_hwdevice_ctx_create')) {
    Assert-True ($symbols -match [regex]::Escape($symbol)) "Simbolo obrigatorio ausente: $symbol"
}

$hash = (Get-FileHash Nplay.nro -Algorithm SHA256).Hash.ToLowerInvariant()
$size = (Get-Item Nplay.nro).Length
Write-Host "OK Nplay $makeVersion | $size bytes | sha256:$hash"
if ($SkipMediaFixtures) { Write-Warning 'Validacao PARCIAL: fixtures reais de midia nao foram executadas.' }
