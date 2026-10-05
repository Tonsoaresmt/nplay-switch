// Execute the real external loader, with deterministic FFmpeg/transport faults.
// This is control-flow/ownership coverage, not a real decoder or Switch test.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
let source = readFileSync('source/player.c', 'utf8');
if (process.argv.includes('--baseline')) {
  const git = process.platform === 'win32' ? 'C:/Program Files/Git/cmd/git.exe' : 'git';
  // Published v0.12.49 target; the local clone may not have release tags.
  const old = spawnSync(git, ['show', '247610129878aaa665e4a147c1698e182f786ba9:source/player.c'], {encoding:'utf8',windowsHide:true,timeout:30000});
  if (old.status !== 0) throw Error(old.stderr);
  source = old.stdout;
}
source = source.replace(/\r\n/g, '\n');
if (!process.argv.includes('--baseline')) {
  const menuStart = source.indexOf('int next = track_sel - 1;');
  const menuEnd = source.indexOf('Uint32 switch_started = SDL_GetTicks();', menuStart);
  const menu = source.slice(menuStart, menuEnd);
  if (menuStart < 0 || menuEnd < menuStart ||
      !menu.includes('next == scur && (next < 0 || !manifest_subtitle_fallback)') ||
      menu.includes('external_subtitle_count'))
    throw Error('Current external subtitle selection must reload even with old cues');
}
const start = source.indexOf('typedef struct { const unsigned char *data;');
const end = source.indexOf('typedef struct {\n    const char *url, *sid;', start);
if (start < 0 || end < start) throw Error('Review external loader boundaries');
mkdirSync('build', {recursive:true});
writeFileSync('build/subtitle_completion.inc', source.slice(start, end));
const exe = resolve('build/test_subtitle_completion'+(process.platform==='win32'?'.exe':''));
const built = spawnSync(process.env.HOST_CC || 'gcc', ['-std=c11','-Wall','-Wextra','-Werror',
  '-Itools/host-stubs','-Iinclude','-Ibuild','tools/subtitle_completion_host.c',
  'source/subtitle_queue.c','source/subtitle_store.c','-lm','-o',exe],
  {encoding:'utf8',windowsHide:true,timeout:30000});
if (built.status !== 0) throw Error(built.stderr || built.error?.message);
const run = spawnSync(exe, [], {encoding:'utf8',windowsHide:true,timeout:15000});
process.stdout.write(run.stdout || '');
if (run.status !== 0) throw Error(run.stderr || run.error?.message || 'Completion regression failed');
