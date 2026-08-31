import { DatabaseSync } from 'node:sqlite';
import { pathToFileURL } from 'node:url';
import { isAbsolute, join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';

// Auditoria somente leitura: nao imprime URLs, tokens ou segredos. Confirma os
// pacotes locais registrados no backend e mede abertura/vazao do delivery R2.

const backend = resolve(process.argv[2] || 'C:/iptv');
// O backend carrega .env/configuracao a partir do cwd. Sem isso o auditor pode
// assinar com o segredo default e produzir um falso HTTP 403.
process.chdir(backend);
const { config } = await import(pathToFileURL(join(backend, 'src/config.js')));
const { decrypt, signMediaDeliveryToken } = await import(pathToFileURL(join(backend, 'src/lib/crypto.js')));
const databasePath = isAbsolute(config.dbPath) ? config.dbPath : resolve(backend, config.dbPath);
const db = new DatabaseSync(databasePath, { readOnly: true });

const rows = db.prepare(`
  SELECT ci.id AS item_id, s.id AS source_id, ci.kind, ci.title, s.stream_url_enc
    FROM item_sources s JOIN catalog_items ci ON ci.id=s.item_id
   WHERE s.active=1 AND s.source_stream_id LIKE 'r2:%' AND s.container_ext='m3u8'
   ORDER BY CASE ci.kind WHEN 'movie' THEN 0 WHEN 'episode' THEN 1 ELSE 2 END, s.id DESC
`).all();
function authorize(raw) {
  const url = new URL(raw);
  const slash = url.pathname.lastIndexOf('/');
  const pathPrefix = url.pathname.slice(0, slash + 1);
  url.searchParams.set('token', signMediaDeliveryToken({
    pathPrefix, userId: 1, deviceId: 1, sessionId: 1, ttl: 600,
  }));
  return url;
}

// Antes de escolher amostras, confirme todos os ponteiros publicados. Um item
// marcado como pronto com manifesto 404 e falha de catalogo, nao de decoder.
const fullAudit = process.argv.includes('--full');
const auditRows = fullAudit ? rows : [
  ...rows.filter((row) => row.kind === 'movie').slice(0, 50),
  ...rows.filter((row) => row.kind === 'episode').slice(0, 50),
];
const health = new Array(auditRows.length);
let cursor = 0;
console.log(`catalogo-r2: verificando ${auditRows.length}/${rows.length} fontes ativas${fullAudit ? ' (auditoria completa)' : ''}...`);
await Promise.all(Array.from({ length: Math.min(16, auditRows.length) }, async () => {
  while (cursor < auditRows.length) {
    const index = cursor++;
    const row = auditRows[index];
    try {
      const response = await fetch(authorize(decrypt(row.stream_url_enc)), {
        method: 'HEAD',
        headers: { 'accept-encoding': 'identity', 'user-agent': 'Nplay-Switch/1.0' },
        signal: AbortSignal.timeout(8000),
      });
      health[index] = { row, status: response.status };
    } catch {
      health[index] = { row, status: 0 };
    }
  }
}));
const healthy = health.filter((entry) => entry?.status === 200);
const unavailable = health.filter((entry) => entry?.status !== 200);
const brokenByStatus = Object.entries(unavailable.reduce((all, entry) => {
  const key = String(entry?.status || 'rede');
  all[key] = (all[key] || 0) + 1;
  return all;
}, {})).map(([status, count]) => `${status}:${count}`).join(',') || 'nenhum';
const missing = unavailable.filter((entry) => entry?.status === 404).length;
const unconfirmed = unavailable.length - missing;
console.log(`catalogo-r2: amostra=${auditRows.length} validos=${healthy.length} 404=${missing} nao-confirmados=${unconfirmed} (${brokenByStatus})`);
const selected = [];
for (const kind of ['movie', 'episode']) {
  const entry = healthy.find((value) => value.row.kind === kind);
  if (entry) selected.push(entry.row);
}
if (!selected.length) throw new Error('Nenhum pacote R2 realmente acessivel no catalogo local.');

function references(manifest) {
  const found = [];
  for (const line of manifest.split(/\r?\n/)) {
    const value = line.trim();
    if (!value) continue;
    if (!value.startsWith('#')) found.push(value);
    for (const match of value.matchAll(/URI="([^"]+)"/g)) found.push(match[1]);
  }
  return [...new Set(found)];
}

async function getText(url) {
  // O AVIO libcurl do Switch solicita blocos por Range inclusive para manifests.
  // Exercite o Worker com os mesmos headers, nao apenas com um GET de navegador.
  const response = await fetch(url, { headers: {
    accept: '*/*',
    'accept-encoding': 'identity',
    range: 'bytes=0-262143',
    'user-agent': 'Nplay-Switch/1.0',
  } });
  const text = await response.text();
  if (response.headers.get('content-encoding')) {
    throw new Error(`manifesto respondeu comprimido apesar de accept-encoding identity`);
  }
  return { response, text };
}

for (const row of selected) {
  const masterUrl = authorize(decrypt(row.stream_url_enc));
  const master = await getText(masterUrl);
  if (!master.response.ok || !master.text.startsWith('#EXTM3U')) {
    throw new Error(`${row.kind}: manifesto principal invalido (HTTP ${master.response.status}, ${master.text.length} bytes)`);
  }
  const masterRefs = references(master.text);
  const variant = master.text.split(/\r?\n/).find((line) => line.startsWith('#EXT-X-STREAM-INF')) || '';
  if (!/RESOLUTION=\d+x\d+/i.test(variant)) {
    throw new Error(`${row.kind}: master sem RESOLUTION para abertura rapida`);
  }
  const childRefs = masterRefs.filter((value) => /\.m3u8(?:[?#]|$)/i.test(value));
  let playlists = 0, objects = 0;
  for (const ref of childRefs) {
    const child = await getText(new URL(ref, masterUrl));
    if (!child.response.ok || !child.text.startsWith('#EXTM3U')) {
      throw new Error(`${row.kind}: playlist filho invalido (HTTP ${child.response.status}, ${child.text.length} bytes)`);
    }
    playlists++;
    const media = references(child.text).find((value) => !/\.m3u8(?:[?#]|$)/i.test(value));
    if (!media) continue;
    const response = await fetch(new URL(media, child.response.url), {
      headers: { range: 'bytes=0-4095', 'accept-encoding': 'identity' },
    });
    if (response.status !== 206 && response.status !== 200) {
      throw new Error(`${row.kind}: objeto de midia indisponivel (HTTP ${response.status})`);
    }
    await response.arrayBuffer();
    objects++;
  }
  console.log(`${row.kind}: master=${master.text.length}B playlists=${playlists} objetos=${objects} codecs=${/CODECS=/i.test(variant) ? 'sim' : 'nao'} status=OK`);

  const started = Date.now();
  const probe = spawnSync(config.storage.ffprobePath || 'ffprobe', [
    '-v', 'error', '-rw_timeout', '30000000', '-seekable', '0',
    '-http_seekable', '0', '-allowed_extensions', 'ALL',
    '-http_persistent', '1', '-http_multiple', '1', '-seg_max_retry', '3',
    '-probesize', '4194304', '-analyzeduration', '3000000',
    '-show_entries', 'stream=index,codec_type,codec_name,width,height',
    '-of', 'json', masterUrl.toString(),
  ], { encoding: 'utf8', timeout: 45_000, windowsHide: true });
  if (probe.status !== 0) {
    const detail = probe.error?.message || probe.stderr || `status ${probe.status}`;
    throw new Error(`${row.kind}: ffprobe falhou (${String(detail).trim().slice(0, 180)})`);
  }
  const streams = JSON.parse(probe.stdout || '{}').streams || [];
  if (!streams.some((stream) => stream.codec_type === 'video')) {
    throw new Error(`${row.kind}: ffprobe nao encontrou video`);
  }
  console.log(`${row.kind}: probe=${Date.now() - started}ms streams=${streams.length} status=OK`);
  const ffmpegPath = String(config.storage.ffprobePath || 'ffprobe').replace(/ffprobe(?:\.exe)?$/i, 'ffmpeg.exe');
  const readStarted = Date.now();
  const read = spawnSync(ffmpegPath, [
    '-v', 'error', '-rw_timeout', '30000000', '-seekable', '0',
    '-http_seekable', '0', '-allowed_extensions', 'ALL',
    '-http_persistent', '1', '-http_multiple', '1', '-seg_max_retry', '3',
    '-probesize', '4194304', '-analyzeduration', '3000000',
    '-i', masterUrl.toString(), '-t', '30', '-map', '0:v:0', '-map', '0:a:0?',
    '-c', 'copy', '-f', 'null', '-',
  ], { encoding: 'utf8', timeout: 45_000, windowsHide: true });
  if (read.status !== 0) {
    const detail = read.error?.message || read.stderr || `status ${read.status}`;
    throw new Error(`${row.kind}: leitura HLS falhou (${String(detail).trim().slice(0, 180)})`);
  }
  const readMs = Date.now() - readStarted;
  console.log(`${row.kind}: leitura=30s em ${readMs}ms (${(30_000 / Math.max(1, readMs)).toFixed(1)}x) status=OK`);
}

const anime = db.prepare(`
  SELECT s.source_stream_id
    FROM item_sources s JOIN catalog_items ci ON ci.id=s.item_id
   WHERE s.active=1 AND s.container_ext='embed' AND ci.kind='episode'
     AND s.source_stream_id GLOB '[0-9]*'
   ORDER BY s.id DESC LIMIT 1
`).get();
if (anime?.source_stream_id) {
  const { resolveHinatasoulForPlayback } = await import(
    pathToFileURL(join(backend, 'src/services/anime-resolver-client.js'))
  );
  const resolved = await resolveHinatasoulForPlayback(anime.source_stream_id, 'FULLHD');
  if (!resolved?.url) throw new Error('anime: resolvedor nao devolveu MP4');
  const response = await fetch(resolved.url, {
    headers: { range: 'bytes=0-524287', 'accept-encoding': 'identity' },
  });
  const body = await response.arrayBuffer();
  const total = response.headers.get('content-range')?.split('/').at(-1) || resolved.bytes || 'desconhecido';
  if (response.status !== 206 || body.byteLength === 0) {
    throw new Error(`anime: Range MP4 invalido (HTTP ${response.status}, ${body.byteLength} bytes)`);
  }
  console.log(`anime: range=${body.byteLength}B total=${total} status=OK`);
}

db.close();
process.exit(0);
