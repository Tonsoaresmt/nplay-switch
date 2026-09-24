import { DatabaseSync } from 'node:sqlite';
import { spawnSync } from 'node:child_process';
import { isAbsolute, join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

// Read-only probe. Never print the signed URL, token, source URL, or credentials.
const backend = resolve(process.argv[2] || 'C:/iptv');
const itemId = Number(process.argv[3]);
const resumeAt = Number(process.argv[4] || 48);
if (!Number.isSafeInteger(itemId) || itemId <= 0 ||
    !Number.isFinite(resumeAt) || resumeAt < 0) {
  throw new Error('usage: node tools/probe_resume_hls.mjs BACKEND ITEM_ID SECONDS');
}
process.chdir(backend);
const { config } = await import(pathToFileURL(join(backend, 'src/config.js')));
const { decrypt, signMediaDeliveryToken } = await import(pathToFileURL(join(backend, 'src/lib/crypto.js')));
const dbPath = isAbsolute(config.dbPath) ? config.dbPath : resolve(backend, config.dbPath);
const db = new DatabaseSync(dbPath, { readOnly: true });
const row = db.prepare(`
  SELECT ci.kind, s.container_ext, s.stream_url_enc
    FROM item_sources s JOIN catalog_items ci ON ci.id=s.item_id
   WHERE s.item_id=? AND s.active=1 AND s.source_stream_id LIKE 'r2:%'
     AND s.container_ext='m3u8'
   ORDER BY s.id DESC LIMIT 1
`).get(itemId);
db.close();
if (!row) throw new Error(`item ${itemId}: no active R2 HLS source in local snapshot`);
const master = new URL(decrypt(row.stream_url_enc));
const prefix = master.pathname.slice(0, master.pathname.lastIndexOf('/') + 1);
master.searchParams.set('token', signMediaDeliveryToken({
  pathPrefix: prefix, userId: 1, deviceId: 1, sessionId: 1, ttl: 600,
}));
const response = await fetch(master, { signal: AbortSignal.timeout(10_000) });
if (!response.ok) throw new Error(`master HTTP ${response.status}`);
const body = await response.text();
if (!body.startsWith('#EXTM3U')) throw new Error('master is not HLS');
const children = [...body.matchAll(/^([^#\r\n][^\r\n]*\.m3u8(?:\?[^\r\n]*)?)$/gm)]
  .map((match) => match[1]);
console.log(`item=${itemId} kind=${row.kind} master=${body.length}B children=${children.length}`);
for (const child of children.slice(0, 3)) {
  const res = await fetch(new URL(child, master), { signal: AbortSignal.timeout(10_000) });
  if (!res.ok) throw new Error(`child HTTP ${res.status}`);
  const text = await res.text();
  const durations = [...text.matchAll(/^#EXTINF:([0-9.]+)/gm)].map((m) => Number(m[1]));
  const total = durations.reduce((a, b) => a + b, 0);
  let n = 0, acc = 0;
  while (n < durations.length && acc + durations[n] <= resumeAt) acc += durations[n++];
  console.log(`child segments=${durations.length} duration=${total.toFixed(1)}s resumeSegment=${n} segmentStart=${acc.toFixed(1)}s map=${text.includes('#EXT-X-MAP')}`);
}
const ffprobe = config.storage.ffprobePath || 'ffprobe';
for (const point of [resumeAt, resumeAt]) {
  const started = Date.now();
  const p = spawnSync(ffprobe, [
    '-v', 'error', '-rw_timeout', '15000000', '-seekable', '0',
    '-http_seekable', '0', '-allowed_extensions', 'ALL',
    '-read_intervals', `${point}%+8`, '-show_entries',
    'stream=index,codec_type:packet=stream_index,pts_time,dts_time,flags', '-of', 'json',
    master.toString(),
  ], { encoding: 'utf8', timeout: 30_000, windowsHide: true });
  const result = p.status === 0 ? JSON.parse(p.stdout || '{}') : {};
  const packets = result.packets || [];
  const first = packets.find((packet) => Number.isFinite(Number(packet.pts_time)));
  const videoIndex = result.streams?.find((stream) => stream.codec_type === 'video')?.index;
  const video = packets.filter((packet) => packet.stream_index === videoIndex &&
    Number.isFinite(Number(packet.pts_time)));
  const firstResumeVideo = video.find((packet) => Number(packet.pts_time) >= resumeAt - 0.15);
  const streams = [...new Set(packets.map((packet) => packet.stream_index))]
    .map((index) => {
      const packet = packets.find((value) => value.stream_index === index && Number.isFinite(Number(value.pts_time)));
      return `${index}:${packet?.pts_time ?? '?'}`;
    });
  const safeError = p.status === 0 ? 'none' : String(p.stderr || p.error?.code || 'ffprobe-failed')
    .replace(/https?:\/\/\S+/g, '[url]').replace(/[\r\n]+/g, ' ').slice(0, 180);
  console.log(`probe at=${point}s status=${p.status ?? 'timeout'} elapsed=${Date.now() - started}ms packets=${packets.length} firstPTS=${first?.pts_time ?? '?'} firstStream=${first?.stream_index ?? '?'} video=${videoIndex ?? '?'} firstResumeVideo=${firstResumeVideo?.pts_time ?? '?'} prerollPackets=${firstResumeVideo ? video.indexOf(firstResumeVideo) : '?'} streams=${streams.join(',')} error=${safeError}`);
  if (p.status !== 0 || !firstResumeVideo) throw new Error('R2 resume packets unavailable');
}
const ffmpeg = String(ffprobe).replace(/ffprobe(?:\.exe)?$/i, 'ffmpeg.exe');
for (const point of [0, resumeAt]) {
  const decodeStarted = Date.now();
  const decode = spawnSync(ffmpeg, [
    '-v', 'error', '-rw_timeout', '15000000', '-seekable', '0',
    '-http_seekable', '0', '-allowed_extensions', 'ALL',
    ...(point > 0 ? ['-ss', String(point)] : []),
    '-i', master.toString(), '-t', '3',
    '-map', '0:v:0', '-an', '-f', 'null', '-',
  ], { encoding: 'utf8', timeout: 35_000, windowsHide: true });
  const decodeError = decode.status === 0 ? 'none' : String(decode.stderr || decode.error?.code || 'ffmpeg-failed')
    .replace(/https?:\/\/\S+/g, '[url]').replace(/[\r\n]+/g, ' ').slice(0, 180);
  console.log(`decode at=${point}s status=${decode.status ?? 'timeout'} elapsed=${Date.now() - decodeStarted}ms error=${decodeError}`);
  if (decode.status !== 0) throw new Error(`R2 decode failed at ${point}s`);
}
