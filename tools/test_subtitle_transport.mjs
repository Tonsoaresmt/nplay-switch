// Execute the production bounded HTTPS entry points against deterministic curl I/O.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
const source = readFileSync('source/net.c', 'utf8').replace(/\r\n/g, '\n');
function section(begin, end) {
  const a = source.indexOf(begin), b = source.indexOf(end, a);
  if (a < 0 || b <= a) throw Error('Review subtitle transport extraction');
  return source.slice(a, b);
}
mkdirSync('build', { recursive: true });
writeFileSync('build/subtitle_transport.inc',
  section('typedef struct { struct membuf *out;', 'long net_request(const char *url,') +
  section('typedef struct {\n    CURL *curl; size_t received, limit;', 'long net_download_file_timeout'));
const exe = resolve('build/test_subtitle_transport.exe');
const built = spawnSync(process.env.HOST_CC || 'gcc', ['-std=c11', '-Wall', '-Wextra', '-Werror', `-I${process.env.HOST_CURL_HEADERS || 'C:/devkitPro/portlibs/switch/include'}`, '-Itools/host-stubs', '-Iinclude', '-Ibuild', 'tools/subtitle_transport_host.c', '-o', exe], { encoding: 'utf8', windowsHide: true, timeout: 30000 });
if (built.status !== 0) throw Error(built.stderr || built.error?.message);
const run = spawnSync(exe, [], { encoding: 'utf8', windowsHide: true, timeout: 10000 });
if (run.status !== 0) throw Error(run.stderr || run.error?.message);
process.stdout.write(run.stdout);
