// Executes the actual main.c sequence with API/player stubs. No Switch rendering.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
let main = readFileSync('source/main.c', 'utf8').replace(/\r\n/g, '\n');
if (process.argv.includes('--baseline')) {
  const old = spawnSync('C:/Program Files/Git/cmd/git.exe', ['show', 'b283f72:source/main.c'], { encoding: 'utf8' });
  if (old.status) throw new Error(old.stderr);
  main = old.stdout.replace(/\r\n/g, '\n');
}
const start = main.indexOf('static void play_episode_sequence(int item_id, int series_id, const char *title,\n                                  cJSON *episode_hint) {');
const end = main.indexOf('\nstatic void input_series(', start);
if (start < 0 || end < 0) throw new Error('Review sequence harness boundaries');
mkdirSync('build', { recursive: true });
writeFileSync('build/episode_sequence.inc', main.slice(start, end));
const exe = resolve('build/test_episode_sequence.exe');
const flags = process.argv.includes('--baseline') ? ['-Wno-unused-function'] : [];
const compiled = spawnSync(process.env.HOST_CC || 'gcc', ['-std=c11', '-Wall', '-Wextra', '-Werror', ...flags, '-Iinclude', '-Ibuild', 'tools/episode_sequence_host.c', 'source/episode_flow.c', 'source/audio_policy.c', 'source/cJSON.c', '-lm', '-o', exe], { encoding: 'utf8', windowsHide: true });
if (compiled.status) throw new Error(compiled.stderr || compiled.error?.message);
const result = spawnSync(exe, [], { encoding: 'utf8', windowsHide: true, timeout: 5000 });
if (result.status) throw new Error(result.stderr || result.error?.message || 'sequence failed');
process.stdout.write(result.stdout);
