#!/usr/bin/env node
// Fluxo local isolado: MKV -> fMP4 chunked -> cliente FFmpeg sem Range.
// Nao toca TorBox, R2, banco ou rede externa.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { resolve } from 'node:path';
import { mkdirSync } from 'node:fs';

const ffmpeg = process.env.FFMPEG_EXE || 'ffmpeg';
const ffprobe = process.env.FFPROBE_EXE || 'ffprobe';
const dir = resolve('build');
mkdirSync(dir, { recursive: true });
const input = resolve(dir, 'test-chunked-input.mkv');
const generated = spawnSync(ffmpeg, [
  '-hide_banner', '-loglevel', 'error', '-y',
  '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=24:duration=3',
  '-f', 'lavfi', '-i', 'sine=frequency=440:duration=3',
  '-c:v', 'libx264', '-preset', 'ultrafast', '-pix_fmt', 'yuv420p',
  '-c:a', 'aac', '-shortest', input,
], { encoding: 'utf8', timeout: 20_000, windowsHide: true });
assert.equal(generated.status, 0, generated.stderr || generated.error?.message);

const server = createServer((_req, response) => {
  response.writeHead(200, {
    'content-type': 'video/mp4',
    'accept-ranges': 'none',
    'cache-control': 'no-store',
  });
  const remux = spawn(ffmpeg, [
    '-hide_banner', '-loglevel', 'error', '-i', input,
    '-map', '0:v:0', '-map', '0:a:0', '-c:v', 'copy',
    '-c:a', 'aac', '-ac', '2', '-b:a', '160k',
    '-fflags', '+genpts+igndts', '-f', 'mp4',
    '-movflags', 'empty_moov+frag_keyframe+default_base_moof+omit_tfhd_offset',
    'pipe:1',
  ], { windowsHide: true, stdio: ['ignore', 'pipe', 'ignore'] });
  remux.stdout.pipe(response);
  response.on('close', () => remux.kill());
});
await new Promise((ok) => server.listen(0, '127.0.0.1', ok));
const port = server.address().port;
try {
  const probe = await new Promise((ok, fail) => {
    const child = spawn(ffprobe, [
      '-v', 'error', '-rw_timeout', '10000000', '-seekable', '0',
      '-show_entries', 'frame=media_type,width,height', '-show_frames',
      '-of', 'json', `http://127.0.0.1:${port}/video`,
    ], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '', stderr = '';
    child.stdout.setEncoding('utf8').on('data', (chunk) => { stdout += chunk; });
    child.stderr.setEncoding('utf8').on('data', (chunk) => { stderr += chunk; });
    child.once('error', fail);
    child.once('close', (code) => code === 0 ? ok(stdout) : fail(new Error(stderr)));
  });
  const frames = JSON.parse(probe).frames || [];
  assert.ok(frames.some((frame) => frame.media_type === 'video' && frame.width === 320),
            'nenhum quadro de video decodificado');
  assert.ok(frames.some((frame) => frame.media_type === 'audio'),
            'nenhum quadro de audio decodificado');
  console.log(`CHUNKED REMUX OK: ${frames.length} quadros reconhecidos sem Range`);
} finally {
  server.close();
}
