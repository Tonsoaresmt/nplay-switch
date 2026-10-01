// Exercise the ACTUAL worker implementation, not a reimplementation of it.
// Generated include is a disposable build artifact. SDL/FFmpeg are host shims;
// pthread mutex/conditions and threads are real.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
const player = readFileSync('source/player.c', 'utf8');
const normalized = player.replace(/\r\n/g, '\n');
const first = normalized.indexOf('typedef struct {\n    AVPacket *packet;');
const end = normalized.indexOf('// A thread de UI permanece viva', first);
const stopBegin = normalized.indexOf('static void demux_worker_stop(');
const stopEnd = normalized.indexOf('static int player_play_internal', stopBegin);
if (first < 0 || end < first || stopBegin < 0 || stopEnd < stopBegin) throw new Error('Worker boundaries changed: review harness');
mkdirSync('build', { recursive: true });
writeFileSync('build/demux_worker_test.inc', normalized.slice(first, end) + normalized.slice(stopBegin, stopEnd));
const compiler = process.env.HOST_CC || 'gcc';
const exe = resolve('build/test_demux_worker' + (process.platform === 'win32' ? '.exe' : ''));
const built = spawnSync(compiler, ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread', '-Iinclude', '-Ibuild', 'tools/demux_worker_host.c', '-lm', '-o', exe], { encoding: 'utf8', windowsHide: true, timeout: 30000 });
if (built.status !== 0) throw new Error(built.error?.message || built.stderr || 'compile failed');
const run = spawnSync(exe, [], { encoding: 'utf8', windowsHide: true, timeout: 15000 });
if (run.status !== 0) throw new Error(run.error?.message || run.stderr || 'worker test failed');
process.stdout.write(run.stdout);
