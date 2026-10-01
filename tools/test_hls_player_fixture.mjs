#!/usr/bin/env node
// Fixture local reproduzivel do contrato R2: HLS fMP4, dois audios e duas
// renditions WebVTT. Nao usa a API, a conta, a rede nem dados do usuario.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import {
  mkdirSync, readFileSync, rmSync, writeFileSync,
} from 'node:fs';
import { join, resolve } from 'node:path';

const ffmpeg = process.env.FFMPEG_EXE || 'ffmpeg';
const ffprobe = process.env.FFPROBE_EXE || 'ffprobe';
const root = resolve('build', 'hls-player-fixture');
rmSync(root, { recursive: true, force: true });
for (const part of ['video', 'audio-pt', 'audio-en', 'sub-pt', 'sub-en']) {
  mkdirSync(join(root, part), { recursive: true });
}

function run(executable, args, label, timeout = 45_000) {
  const result = spawnSync(executable, args, {
    encoding: 'utf8', timeout, windowsHide: true,
  });
  assert.equal(result.status, 0,
    `${label} falhou:\n${result.stderr || result.error?.message || 'sem detalhe'}`);
  return result.stdout;
}

function hlsArgs(outputDir) {
  return [
    '-hls_time', '2', '-hls_playlist_type', 'vod',
    '-hls_segment_type', 'fmp4',
    '-hls_fmp4_init_filename', join(outputDir, 'init.mp4'),
    '-hls_segment_filename', join(outputDir, 'seg-%03d.m4s'),
    join(outputDir, 'index.m3u8'),
  ];
}

function normalizeInitUri(outputDir) {
  const playlist = join(outputDir, 'index.m3u8');
  const text = readFileSync(playlist, 'utf8');
  writeFileSync(playlist, text.replace(/#EXT-X-MAP:URI="[^"]+"/, '#EXT-X-MAP:URI="init.mp4"'));
}

run(ffmpeg, [
  '-hide_banner', '-loglevel', 'error', '-y',
  '-f', 'lavfi', '-i', 'testsrc2=size=640x360:rate=24:duration=12',
  '-an', '-c:v', 'libx264', '-preset', 'ultrafast', '-pix_fmt', 'yuv420p',
  '-g', '48', '-keyint_min', '48', '-sc_threshold', '0',
  ...hlsArgs(join(root, 'video')),
], 'video fMP4');
normalizeInitUri(join(root, 'video'));

for (const [folder, frequency] of [['audio-pt', '440'], ['audio-en', '660']]) {
  run(ffmpeg, [
    '-hide_banner', '-loglevel', 'error', '-y',
    '-f', 'lavfi', '-i', `sine=frequency=${frequency}:sample_rate=48000:duration=12`,
    '-vn', '-c:a', 'aac', '-b:a', '96k', '-ac', '2', '-ar', '48000',
    ...hlsArgs(join(root, folder)),
  ], `${folder} fMP4`);
  normalizeInitUri(join(root, folder));
}

function writeSubtitle(folder, language, first, second) {
  writeFileSync(join(root, folder, 'index.m3u8'), [
    '#EXTM3U', '#EXT-X-VERSION:3', '#EXT-X-TARGETDURATION:12',
    '#EXT-X-MEDIA-SEQUENCE:0', '#EXT-X-PLAYLIST-TYPE:VOD',
    '#EXTINF:12.000,', 'cues.vtt', '#EXT-X-ENDLIST', '',
  ].join('\n'));
  writeFileSync(join(root, folder, 'cues.vtt'), [
    'WEBVTT', 'X-TIMESTAMP-MAP=MPEGTS:0,LOCAL:00:00:00.000', '',
    '00:00:01.000 --> 00:00:03.500', first, '',
    '00:00:06.000 --> 00:00:09.000', second, '',
  ].join('\n'));
  return language;
}
writeSubtitle('sub-pt', 'pt-BR', 'Legenda em portugues', 'Continua depois do seek');
writeSubtitle('sub-en', 'en', 'English subtitle', 'Still visible after seek');

const master = join(root, 'master.m3u8');
writeFileSync(master, [
  '#EXTM3U', '#EXT-X-VERSION:7',
  '#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aud",NAME="English",DEFAULT=YES,AUTOSELECT=YES,LANGUAGE="en",URI="audio-en/index.m3u8"',
  '#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aud",NAME="Portugues (Brasil)",DEFAULT=NO,AUTOSELECT=YES,LANGUAGE="pt-BR",URI="audio-pt/index.m3u8"',
  '#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="Portugues (Brasil)",DEFAULT=NO,AUTOSELECT=YES,FORCED=NO,LANGUAGE="pt-BR",URI="sub-pt/index.m3u8"',
  '#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="English",DEFAULT=NO,AUTOSELECT=YES,FORCED=NO,LANGUAGE="en",URI="sub-en/index.m3u8"',
  '#EXT-X-STREAM-INF:BANDWIDTH=900000,AVERAGE-BANDWIDTH=750000,RESOLUTION=640x360,FRAME-RATE=24.000,CODECS="avc1.42c01e,mp4a.40.2",AUDIO="aud",SUBTITLES="subs"',
  'video/index.m3u8', '',
].join('\n'));

const probe = JSON.parse(run(ffprobe, [
  '-v', 'error', '-show_entries',
  'stream=index,codec_type,codec_name,time_base:stream_tags=language,title,comment,name',
  '-show_streams', '-of', 'json', master,
], 'enumeracao do master'));
const streams = probe.streams || [];
const video = streams.filter((stream) => stream.codec_type === 'video');
const audios = streams.filter((stream) => stream.codec_type === 'audio');
const subtitles = streams.filter((stream) => stream.codec_type === 'subtitle');
assert.equal(video.length, 1, `video esperado=1 recebido=${video.length}`);
assert.equal(audios.length, 2, `audios esperados=2 recebidos=${audios.length}`);
assert.equal(subtitles.length, 2, `legendas esperadas=2 recebidas=${subtitles.length}`);
assert.deepEqual(new Set(audios.map((stream) => stream.tags?.language)), new Set(['en', 'pt-BR']));
assert.deepEqual(new Set(subtitles.map((stream) => stream.tags?.language)), new Set(['en', 'pt-BR']));
const ptAudio = audios.find((stream) => stream.tags?.language === 'pt-BR');
const ptSubtitle = subtitles.find((stream) => stream.tags?.language === 'pt-BR');
assert.equal(ptAudio?.tags?.comment, 'Portugues (Brasil)',
  'FFmpeg expoe NAME da rendition de audio HLS em comment');
assert.equal(ptSubtitle?.tags?.comment, 'Portugues (Brasil)',
  'FFmpeg expoe NAME da rendition de legenda HLS em comment');
assert.ok(subtitles.every((stream) => stream.codec_name === 'webvtt'),
  'todas as legendas precisam ser WebVTT');
assert.ok(streams.every((stream) => stream.time_base),
  'todas as faixas precisam expor time_base ao decoder');

// Decodifica inicio e um ponto apos seek. Isso pega manifests bem formados que
// apenas enumeram as faixas, mas falham ao abrir init/segmento ou ao reposicionar.
for (const [label, seek] of [['inicio', '0'], ['apos seek', '6']]) {
  run(ffmpeg, [
    '-hide_banner', '-loglevel', 'error', '-ss', seek, '-i', master,
    '-map', '0:v:0', '-map', '0:a:0', '-map', '0:a:1',
    '-t', '2', '-f', 'null', '-',
  ], `decode ${label}`);
}

// Estressa saltos para frente/tras com as duas renditions que o cliente precisa
// preservar ao trocar faixa ou reconstruir uma sessao.
for (const [seek, audio] of [['2', '0:a:1'], ['8', '0:a:0'], ['4', '0:a:1']]) {
  run(ffmpeg, [
    '-hide_banner', '-loglevel', 'error', '-ss', seek, '-i', master,
    '-map', '0:v:0', '-map', audio, '-t', '1', '-f', 'null', '-',
  ], `stress seek=${seek} audio=${audio}`);
}

const decodedSubtitles = run(ffmpeg, [
  '-hide_banner', '-loglevel', 'error', '-i', master,
  '-map', '0:s:0', '-c:s', 'webvtt', '-f', 'webvtt', '-',
], 'decode WebVTT');
assert.match(decodedSubtitles, /00:01\.000 --> 00:03\.500/,
  'primeiro cue perdeu PTS/duracao');
assert.match(decodedSubtitles, /00:06\.000 --> 00:09\.000/,
  'segundo cue perdeu PTS/duracao');
assert.match(decodedSubtitles, /Legenda em portugues/,
  'texto WebVTT em portugues nao foi decodificado');

console.log(`HLS PLAYER FIXTURE OK: ${streams.length} faixas, 2 cues, inicio+seek decodificados`);

// Direct VTT used by remux: no HLS demuxer, preserve overlapping intervals.
function directVttPackets(text) {
  const result = spawnSync(ffprobe, ['-v','error','-f','webvtt','-show_packets',
    '-show_entries','packet=pts_time,duration_time','-of','json','pipe:0'],
    {input:text,encoding:'utf8',timeout:10000,windowsHide:true});
  if (result.status !== 0) return [];
  return JSON.parse(result.stdout).packets || [];
}
const direct = directVttPackets('\ufeffWEBVTT\n\n00:01.000 --> 00:05.000\nOlá\nsegunda linha\n\n00:03.000 --> 00:06.000\nSobreposta\n\n');
assert.equal(direct.length, 2);
assert.deepEqual(direct.map(p=>[Number(p.pts_time),Number(p.duration_time)]), [[1,4],[3,3]]);
assert.equal(directVttPackets('WEBVTT\n\n').length, 0);
assert.equal(directVttPackets('WEBVTT\n\n00:01.000 -->').length, 0);
assert.equal(directVttPackets('<html>503 indisponivel</html>').length, 0);
console.log('DIRECT VTT FIXTURE OK: BOM, UTF-8, multiline, overlap, empty and malformed/truncated timing');
