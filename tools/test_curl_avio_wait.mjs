// Execute the actual transport read callback, with deterministic SDL waits.
// --baseline checks the committed version and must fail on the >300ms gap.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
let source = readFileSync('source/curl_avio.c', 'utf8');
if (process.argv.includes('--baseline')) {
  const old = spawnSync('C:/Program Files/Git/cmd/git.exe', ['show', '48a75f9:source/curl_avio.c'], { encoding: 'utf8' });
  if (old.status) throw new Error(old.stderr);
  source = old.stdout;
}
source = source.replace(/\r\n/g, '\n');
const structBegin = source.indexOf('typedef struct {\n    CURL *easy;');
const structEnd = source.indexOf('} CurlIO;', structBegin) + '} CurlIO;'.length;
const begin = source.indexOf('static int cio_read(');
const end = source.indexOf('static int64_t cio_seek(', begin);
if (structBegin < 0 || begin < 0 || end < begin) throw new Error('Review transport harness boundaries');
mkdirSync('build', { recursive: true });
writeFileSync('build/curl_avio_read_test.inc', (source.includes('last_body_tick') ? '#define TRANSPORT_IDLE_GUARD 1\n' : '') + source.slice(structBegin, structEnd));
writeFileSync('build/curl_avio_read_function.inc', source.slice(begin, end));
const progressBegin = source.indexOf('static int xfer_cb(');
const progressEnd = source.indexOf('static int fetch_block(', progressBegin);
if (progressBegin < 0 || progressEnd < progressBegin) throw new Error('Review progress harness boundaries');
writeFileSync('build/curl_avio_progress_function.inc', source.slice(progressBegin, progressEnd));
const ringBegin = source.indexOf('static void ring_put(');
const ringEnd = source.indexOf('static int fetch_stream(', ringBegin);
writeFileSync('build/curl_avio_ring_function.inc', source.slice(ringBegin, ringEnd));
const exe = resolve('build/test_curl_avio_wait.exe');
const cc = process.env.HOST_CC || 'gcc';
const build = spawnSync(cc, ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Ibuild', 'tools/curl_avio_wait_host.c', '-o', exe], { encoding: 'utf8', windowsHide: true });
if (build.status) throw new Error(build.stderr || build.error?.message);
const run = spawnSync(exe, [], { encoding: 'utf8', windowsHide: true, timeout: 5000 });
if (run.status) throw new Error(run.stderr || run.error?.message || 'transport wait failed');
process.stdout.write(run.stdout);
