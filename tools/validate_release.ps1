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

$apiSource = Get-Content source/api.c -Raw
Assert-True ($apiSource -match '/api/stream/session/%d/refresh') 'Refresh da mesma sessao nao esta implementado.'
Assert-True ($apiSource -match '/api/stream/session/%d/heartbeat') 'Heartbeat da sessao nao esta implementado.'
Assert-True ($apiSource -match '/api/sync/progress') 'Progresso periodico nao esta implementado.'
Assert-True ($apiSource -match 'api_reresolve_playback') 'Nova resolucao curta para recuperacao nao esta implementada.'

$tlsPatch = Get-Content tools/ffmpeg-libnx-tls-hostname.patch -Raw
$ffmpegBuild = Get-Content tools/build_ffmpeg_https.sh -Raw
Assert-True ($tlsPatch -match 'SslVerifyOption_PeerCa \| SslVerifyOption_HostName') 'Patch TLS nao valida CA e hostname juntos.'
Assert-True ($ffmpegBuild -match 'ffmpeg-libnx-tls-hostname\.patch') 'Build do FFmpeg nao aplica o patch TLS local.'

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
