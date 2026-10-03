// Execute actual player_run with scripted pipeline/network responses.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
let p = readFileSync('source/player.c', 'utf8');
if (process.argv.includes('--baseline')) {
  const old = spawnSync('C:/Program Files/Git/cmd/git.exe', ['show', '7c85e75:source/player.c'], { encoding: 'utf8' });
  if (old.status) throw new Error(old.stderr);
  p = old.stdout;
}
p = p.replace(/\r\n/g, '\n');
if (!process.argv.includes('--baseline')) {
  if (p.includes('resume-from-start') || p.includes('attempt_start = 0')) throw new Error('Automatic zero-position fallback returned');
  if (!p.includes('seek", "resume-rejected"') || !p.includes('if (resume_preroll && reached_end)')) throw new Error('Failed seek/empty resume guard missing');
  if (!p.includes('if (running && !sequential_stream && start_sec > 0)')) throw new Error('Short/near-end resume skipped');
}
const begin = p.indexOf('int player_run(');
const hb = p.indexOf('typedef struct {\n    int item_id;\n    SDL_atomic_t session_id;');
const hbEnd = p.indexOf('} PlaybackHeartbeat;', hb) + '} PlaybackHeartbeat;'.length;
if (begin < 0 || hb < 0 || hbEnd < hb) throw new Error('Review harness boundaries');
const api = readFileSync('include/api.h', 'utf8');
const head = readFileSync('include/player.h', 'utf8').replace(/^#pragma once.*$/m, '').replace(/^#include.*$/gm, '');
mkdirSync('build', { recursive: true });
writeFileSync('build/player_supervisor_types.inc', api.slice(api.indexOf('typedef enum {'), api.indexOf('} PlaybackSource;') + '} PlaybackSource;'.length) + '\n' + head + '\n' + p.slice(hb, hbEnd));
writeFileSync('build/player_supervisor_function.inc', p.slice(begin));
const exe = resolve('build/test_player_supervisor.exe');
const compiled = spawnSync(process.env.HOST_CC || 'gcc', ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Iinclude', '-Ibuild', 'tools/player_supervisor_host.c', 'source/player_sync.c', '-o', exe], { encoding: 'utf8', windowsHide: true });
if (compiled.status) throw new Error(compiled.stderr || compiled.error?.message);
const run = spawnSync(exe, [], { encoding: 'utf8', windowsHide: true, timeout: 5000 });
if (run.status) throw new Error(run.stderr || run.error?.message || 'supervisor failed');
process.stdout.write(run.stdout);
