param([string]$BackendRoot = 'C:\iptv')

$ErrorActionPreference = 'Stop'
$appRoot = Split-Path -Parent $PSScriptRoot
$switchMain = Get-Content (Join-Path $appRoot 'source/main.c') -Raw
$switchApi = Get-Content (Join-Path $appRoot 'source/api.c') -Raw
$switchPlayer = Get-Content (Join-Path $appRoot 'source/player.c') -Raw

function Assert-Contains([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

foreach ($endpoint in @(
    '/api/catalog/home',
    '/api/catalog/tab-home\?tab=movie',
    '/api/catalog/tab-home\?tab=series',
    '/api/catalog/anime-home',
    '/api/catalog/tab-home\?tab=dorama',
    '/api/catalog/search-v2\?q='
)) {
    Assert-Contains $switchMain $endpoint "Endpoint ausente no Switch: $endpoint"
}
Assert-Contains $switchApi '/api/stream/session/%d/refresh' 'Refresh de sessao ausente no Switch.'
Assert-Contains $switchApi '/api/stream/session/%d/fail' 'Failover de fonte ausente no Switch.'
Assert-Contains $switchApi '/api/stream/session/%d/heartbeat' 'Heartbeat ausente no Switch.'
Assert-Contains $switchPlayer 'fallback_cb' 'Supervisor do player nao usa failover.'
Assert-Contains $switchMain 'search_scope' 'Busca do Switch nao separa as areas do catalogo.'
Assert-Contains $switchMain 'url_encode_utf8' 'Busca do Switch nao codifica UTF-8 com seguranca.'

if (-not (Test-Path $BackendRoot)) {
    Write-Host "OK contrato interno do Switch; backend local nao encontrado em $BackendRoot"
    exit 0
}

$catalog = Get-Content (Join-Path $BackendRoot 'src/routes/catalog.js') -Raw
$search = Get-Content (Join-Path $BackendRoot 'src/routes/catalog-search.js') -Raw
$stream = Get-Content (Join-Path $BackendRoot 'src/routes/stream.js') -Raw
Assert-Contains $catalog "app\.get\('/tab-home'" 'Backend nao possui tab-home.'
Assert-Contains $catalog "app\.get\('/anime-home'" 'Backend nao possui anime-home.'
Assert-Contains $catalog "app\.get\('/series/:id'" 'Backend nao possui detalhe de series.'
Assert-Contains $search "app\.get\('/search-v2'" 'Backend nao possui busca v2.'
Assert-Contains $search "WHEN c\.section='anime' THEN 'anime'" 'Busca v2 nao classifica anime.'
Assert-Contains $search "WHEN c\.section='dorama' THEN 'dorama'" 'Busca v2 nao classifica dorama.'
Assert-Contains $stream "'/stream/session/:sessionId/refresh'" 'Backend nao possui refresh de sessao.'
Assert-Contains $stream "'/stream/session/:sessionId/fail'" 'Backend nao possui failover de fonte.'
Assert-Contains $stream "'/stream/session/:sessionId/heartbeat'" 'Backend nao possui heartbeat.'
foreach ($field in @('delivery:', 'container:', 'play_url:')) {
    Assert-Contains $stream ([regex]::Escape($field)) "Descritor de stream sem campo $field"
}

Write-Host 'OK contrato site <-> Switch: catalogos, busca contextual e sessao de playback'
