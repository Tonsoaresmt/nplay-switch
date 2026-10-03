// Real host libcurl, localhost only; scaled timeout tests a blocked write callback.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { spawn, spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
import { readFileSync, writeFileSync } from 'node:fs';
const transport = readFileSync('source/curl_avio.c', 'utf8').replace(/\r\n/g, '\n');
const structStart = transport.indexOf('typedef struct {\n    CURL *easy;');
const progressStart = transport.indexOf('static int xfer_cb(');
assert.ok(structStart >= 0 && progressStart >= 0);
writeFileSync('build/curl_avio_read_test.inc', transport.slice(structStart, transport.indexOf('} CurlIO;', structStart) + '} CurlIO;'.length));
writeFileSync('build/curl_avio_progress_function.inc', transport.slice(progressStart, transport.indexOf('static int fetch_block(', progressStart)));
const exe = resolve('build/curl_backpressure_probe.exe');
const cc = process.env.HOST_CC || 'gcc';
const headers = process.env.HOST_CURL_HEADERS || 'C:/devkitPro/portlibs/switch/include';
const lib = process.env.HOST_CURL_LIBRARY || 'C:/devkitPro/msys2/usr/bin/msys-curl-4.dll';
const compile = spawnSync(cc, ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Ibuild', `-I${headers}`, 'tools/curl_backpressure_probe.c', lib, '-o', exe], { encoding: 'utf8', windowsHide: true });
assert.equal(compile.status, 0, compile.stderr || compile.error?.message);
const server = createServer((req, res) => {
  res.writeHead(200, { 'Content-Length': '1024' });
  res.write(Buffer.alloc(128));
  if (req.url === '/idle') return;
  const first = setTimeout(() => res.write(Buffer.alloc(384)), 1000);
  const last = setTimeout(() => res.end(Buffer.alloc(512)), 4200);
  res.on('close', () => { clearTimeout(first); clearTimeout(last); });
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
try {
  for (const seconds of ['2', '0', 'idle']) {
    const p = spawn(exe, [`http://127.0.0.1:${server.address().port}/${seconds === 'idle' ? 'idle' : 'fixture'}`, seconds], { windowsHide: true });
    let output = ''; p.stdout.on('data', chunk => output += chunk);
    const rc = await new Promise((ok, fail) => { p.on('error', fail); p.on('close', ok); });
    console.log(`low-speed=${seconds}s ${output.trim()}`);
    assert.equal(rc, seconds === '0' ? 0 : 1);
    assert.match(output, seconds === '2' ? /curl=28/ : seconds === 'idle' ? /curl=42/ : /curl=0 bytes=1024/);
  }
} finally { server.closeAllConnections(); await new Promise(r => server.close(r)); }
