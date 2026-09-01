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
Assert-True ($sources -match 'avformat_open_input\(&fmt, url, NULL') 'Fonte remota nao usa o HTTPS nativo do FFmpeg.'
Assert-True ($sources -match 'CURLOPT_ACCEPT_ENCODING, "identity"') 'Ranges do MP4 podem ser alterados por compressao HTTP.'
Assert-True ($sources -match 'fmt->io_open = player_hls_io_open') 'HLS voltou a depender do HTTPS interno do FFmpeg/libnx.'
Assert-True ($sources -match 'nplay_curl_avio_open_hls') 'Playlists e segmentos HLS nao usam o transporte libcurl.'
Assert-True ($sources -match 'http_persistent", "0"') 'HLS customizado tentou reutilizar um AVIO como protocolo HTTP nativo.'
Assert-True ($sources -match 'fmt->video_codec_id = AV_CODEC_ID_H264') 'R2 sem CODECS voltou a exigir sondagem completa de video.'
Assert-True ($sources -match 'fmt->audio_codec_id = AV_CODEC_ID_AAC') 'R2 sem CODECS voltou a exigir sondagem completa de audio.'
Assert-True ($sources -match 'SDL_UpdateNVTexture') 'Player perdeu o upload NV12 direto do decoder por hardware.'
Assert-True ($sources -match 'attempt\.playback = active') 'Tentativa recuperada nao recebe o descritor atualizado.'
Assert-True ($sources -match 'SDL_JoystickGetButton\(watch->joy, JOY_B\)') 'Preparacao do player nao pode ser cancelada por B.'
Assert-True ($sources -match 'pipeline_ready') 'Heartbeat pode voltar a disputar rede durante a abertura.'
Assert-True ($sources -match 'retry_limit = startup_failure \? 2 : 3') 'Falha inicial voltou a encerrar antes de tentar a fonte alternativa.'
Assert-True ($sources -match 'player_boot_stage\("03 abrindo fonte"\)') 'Crash do player voltou a nao deixar diagnostico persistente.'
Assert-True ($sources -match 'sdmc:/switch/\.nplay-player-boot\.txt') 'Diagnostico de crash depende de uma subpasta opcional.'

$mainSource = Get-Content source/main.c -Raw
Assert-True ($mainSource -match 'SDL_CreateThread\(landing_fetch_thread') 'Catalogo voltou a bloquear a thread de interface.'
Assert-True ($mainSource -match 'g_land_cache\[5\]') 'Troca de aba perdeu o cache de catalogo.'
Assert-True ($mainSource -match 'api_get_timeout\(landing_path\(tab\), 6L, 30L\)') 'Series voltou ao timeout curto ou sincrono.'
Assert-True ($mainSource -match 'load_player_boot_stage') 'A ultima etapa antes de um crash nao aparece no diagnostico.'

$apiSource = Get-Content source/api.c -Raw
Assert-True ($apiSource -match '/api/stream/session/%d/refresh') 'Refresh da mesma sessao nao esta implementado.'
Assert-True ($apiSource -match '/api/stream/session/%d/fail') 'Failover para outra fonte nao esta implementado.'
Assert-True ($apiSource -match '/api/stream/session/%d/heartbeat') 'Heartbeat da sessao nao esta implementado.'
Assert-True ($apiSource -match '/api/sync/progress') 'Progresso periodico nao esta implementado.'
Assert-True ($apiSource -match 'api_reresolve_playback') 'Nova resolucao curta para recuperacao nao esta implementada.'

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
$nm = 'C:\devkitPro\devkitA64\bin\aarch64-none-elf-nm.exe'
Assert-True (Test-Path $nm) 'aarch64-none-elf-nm nao encontrado.'
$symbols = (& $nm Nplay.elf) -join "`n"
foreach ($symbol in @('ff_https_protocol','ff_hls_demuxer','ff_h264_nvtegra_hwaccel','av_hwdevice_ctx_create')) {
    Assert-True ($symbols -match [regex]::Escape($symbol)) "Simbolo obrigatorio ausente: $symbol"
}

$hash = (Get-FileHash Nplay.nro -Algorithm SHA256).Hash.ToLowerInvariant()
$size = (Get-Item Nplay.nro).Length
Write-Host "OK Nplay $makeVersion | $size bytes | sha256:$hash"
