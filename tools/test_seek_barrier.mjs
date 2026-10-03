import { readFileSync, writeFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
let p = readFileSync('source/player.c', 'utf8');
if (process.argv.includes('--baseline')) {
  const old = spawnSync('C:/Program Files/Git/cmd/git.exe', ['show', 'd9c2df6:source/player.c'], { encoding: 'utf8' });
  if (old.status) throw new Error(old.stderr); p = old.stdout;
}
const start = p.indexOf('static int player_seek_with_barrier(');
const end = p.indexOf('static void demux_worker_stop(', start);
if (start < 0 || end < start) throw new Error('Review seek harness boundaries');
writeFileSync('build/seek_barrier_function.inc', p.slice(start, end));
const exe = resolve('build/test_seek_barrier.exe');
const cc = spawnSync(process.env.HOST_CC || 'gcc', ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Ibuild', 'tools/seek_barrier_host.c', '-o', exe], { encoding: 'utf8', windowsHide: true });
if (cc.status) throw new Error(cc.stderr || cc.error?.message);
const run = spawnSync(exe, [], { encoding: 'utf8', windowsHide: true, timeout: 5000 });
if (run.status) throw new Error(run.stderr || run.error?.message);
process.stdout.write(run.stdout);
