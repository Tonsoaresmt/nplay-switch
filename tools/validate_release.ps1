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
Assert-True ($sources -notmatch 'request->userdata\s*=') 'O player voltou a sobrescrever userdata do chamador.'
Assert-True ($sources -match 'AVIOContext \*avio = NULL') 'MP4 remoto voltou ao AVIO por blocos que falha no Switch.'
Assert-True ($sources -match 'avformat_open_input\(&fmt, url, NULL') 'Fonte remota nao usa o HTTPS nativo do FFmpeg.'
Assert-True ($sources -match 'CURLOPT_ACCEPT_ENCODING, "identity"') 'Ranges do MP4 podem ser alterados por compressao HTTP.'
Assert-True ($sources -match 'http_multiple", "1"') 'HLS nao habilita conexoes simultaneas para video/audio.'
Assert-True ($sources -match 'http_persistent", "1"') 'HLS nao reutiliza conexoes entre segmentos.'
Assert-True ($sources -match 'fmt->video_codec_id = AV_CODEC_ID_H264') 'R2 sem CODECS voltou a exigir sondagem completa de video.'
Assert-True ($sources -match 'fmt->audio_codec_id = AV_CODEC_ID_AAC') 'R2 sem CODECS voltou a exigir sondagem completa de audio.'
Assert-True ($sources -match 'SDL_UpdateNVTexture') 'Player perdeu o upload NV12 direto do decoder por hardware.'
Assert-True ($sources -match 'attempt\.playback = active') 'Tentativa recuperada nao recebe o descritor atualizado.'
Assert-True ($sources -match 'SDL_JoystickGetButton\(watch->joy, JOY_B\)') 'Preparacao do player nao pode ser cancelada por B.'
Assert-True ($sources -match 'pipeline_ready') 'Heartbeat pode voltar a disputar rede durante a abertura.'

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
