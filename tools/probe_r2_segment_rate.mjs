#!/usr/bin/env node
// Somente leitura; nao exibe URL assinada, token ou nome de obra.
import { DatabaseSync } from 'node:sqlite';
import { pathToFileURL } from 'node:url';
import { isAbsolute, join, resolve } from 'node:path';

const backend = resolve(process.argv[2] || 'C:/iptv');
process.chdir(backend);
const { config } = await import(pathToFileURL(join(backend, 'src/config.js')));
const { decrypt, signMediaDeliveryToken } = await import(pathToFileURL(join(backend, 'src/lib/crypto.js')));
const dbPath = isAbsolute(config.dbPath) ? config.dbPath : resolve(backend, config.dbPath);
const db = new DatabaseSync(dbPath, { readOnly: true });
const query = db.prepare(`
  SELECT ci.id, ci.kind, s.stream_url_enc
    FROM item_sources s JOIN catalog_items ci ON ci.id=s.item_id
   WHERE s.active=1 AND s.container_ext='m3u8' AND s.source_stream_id LIKE 'r2:%'
     AND ci.kind=?
   ORDER BY s.id DESC LIMIT 12
`);
const rows = [...query.all('movie'), ...query.all('episode')];

function signed(raw) {
  const url = new URL(decrypt(raw));
  url.searchParams.set('token', signMediaDeliveryToken({
    pathPrefix: url.pathname.slice(0, url.pathname.lastIndexOf('/') + 1),
    userId: 1, deviceId: 1, sessionId: 1, ttl: 600,
  }));
  return url;
}
async function get(url, timeout = 15_000) {
  const start = performance.now();
  const response = await fetch(url, {
    headers: { 'accept-encoding': 'identity', 'user-agent': 'Nplay-Switch/1.0' },
    signal: AbortSignal.timeout(timeout),
  });
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  const data = await response.arrayBuffer();
  return { response, bytes: data.byteLength, ms: Math.round(performance.now() - start),
           text: () => new TextDecoder().decode(data) };
}
function childReference(master) {
  const lines = master.split(/\r?\n/);
  for (let i = 0; i < lines.length - 1; i++)
    if (lines[i].startsWith('#EXT-X-STREAM-INF:') && lines[i + 1] && !lines[i + 1].startsWith('#'))
      return { meta: lines[i], ref: lines[i + 1] };
  throw new Error('sem playlist de video');
}
function segments(playlist) {
  const lines = playlist.split(/\r?\n/);
  const found = [];
  for (let i = 0; i < lines.length - 1; i++) {
    if (!lines[i].startsWith('#EXTINF:')) continue;
    const seconds = Number(lines[i].slice(8).split(',')[0]);
    const ref = lines[i + 1];
    if (Number.isFinite(seconds) && ref && !ref.startsWith('#')) found.push({ seconds, ref });
  }
  return found.slice(0, 6);
}

const selected = [];
for (const row of rows) {
  if (selected.some((item) => item.kind === row.kind)) continue;
  try {
    const masterUrl = signed(row.stream_url_enc);
    const response = await fetch(masterUrl, { method: 'HEAD', signal: AbortSignal.timeout(6000) });
    if (response.status === 200) selected.push({ id: row.id, kind: row.kind, masterUrl });
  } catch {}
  if (selected.length === 2) break;
}
db.close();
if (!selected.length) throw new Error('nenhum pacote R2 acessivel na amostra');
for (const sample of selected) {
  try {
    const master = await get(sample.masterUrl);
    const child = childReference(master.text());
    async function measure(ref, track) {
      const childUrl = new URL(ref, sample.masterUrl);
      const media = await get(childUrl);
      const selectedSegments = segments(media.text());
      if (!selectedSegments.length) throw new Error('playlist sem segmentos');
      let bytes = 0, wallMs = 0, mediaSeconds = 0;
      for (const [index, segment] of selectedSegments.entries()) {
        let response;
        try { response = await get(new URL(segment.ref, childUrl), 20_000); }
        catch (error) { throw new Error(`${track} segmento ${index + 1}: ${error?.message || error}`); }
        bytes += response.bytes;
        wallMs += response.ms;
        mediaSeconds += segment.seconds;
      }
      return { bytes, wallMs, mediaSeconds };
    }
    const video = await measure(child.ref, 'video');
    const audioRef = master.text().match(/#EXT-X-MEDIA:TYPE=AUDIO[^\r\n]*URI="([^"]+)"/)?.[1];
    const audio = audioRef ? await measure(audioRef, 'audio') : null;
    const bandwidth = child.meta.match(/BANDWIDTH=(\d+)/)?.[1] || '?';
    const resolution = child.meta.match(/RESOLUTION=([^,]+)/)?.[1] || '?';
    console.log(`${sample.kind} ${sample.id}: ${resolution} bw=${bandwidth}bps `
      + `video=${video.mediaSeconds.toFixed(1)}s/${(video.wallMs / 1000).toFixed(1)}s `
      + `(${(video.mediaSeconds * 1000 / Math.max(1, video.wallMs)).toFixed(2)}x) `
      + `audio=${audio ? `${audio.mediaSeconds.toFixed(1)}s/${(audio.wallMs / 1000).toFixed(1)}s (${(audio.mediaSeconds * 1000 / Math.max(1, audio.wallMs)).toFixed(2)}x)` : 'integrado'}`);
  } catch (error) {
    console.log(`${sample.kind} ${sample.id}: falha de rede/entrega ${String(error?.message || error).slice(0, 100)}`);
  }
}
