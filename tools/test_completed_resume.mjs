// Executes the actual main.c progress decisions, with UI/network stubs.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
let main = readFileSync('source/main.c', 'utf8').replace(/\r\n/g, '\n');
if (process.argv.includes('--baseline')) {
  const old = spawnSync('C:/Program Files/Git/cmd/git.exe', ['show', 'ca5a7a8:source/main.c'], { encoding: 'utf8' });
  if (old.status) throw new Error(old.stderr);
  main = old.stdout.replace(/\r\n/g, '\n');
}
const directStart = main.indexOf('static int play_with_progress_details(int itemId, const char *title, const char *url,\n                                      int is_hls, const PlaybackPresentation *presentation) {');
const directBegin = main.indexOf('    double start = 0;', directStart);
const directEnd = main.indexOf('    PlayerRequest req = {0};', directBegin);
const resolvedStart = main.indexOf('static int resolve_and_play_resolved(int itemId, char *stable_title, char *stable_subtitle,', main.indexOf('int resolve_and_play_details('));
const resolvedBegin = main.indexOf('        if (pr) {', resolvedStart);
const resolvedEnd = main.indexOf('        PlayerRequest req = {0};', resolvedBegin);
if ([directStart, directBegin, directEnd, resolvedStart, resolvedBegin, resolvedEnd].some(x => x < 0)) throw new Error('Review progress extraction boundaries');
mkdirSync('build', { recursive: true });
writeFileSync('build/completed_resume_direct.inc', main.slice(directBegin, directEnd));
writeFileSync('build/completed_resume_resolved.inc', main.slice(resolvedBegin, resolvedEnd));
const exe = resolve('build/test_completed_resume.exe');
const built = spawnSync(process.env.HOST_CC || 'gcc', ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Iinclude', '-Ibuild', 'tools/completed_resume_host.c', 'source/cJSON.c', '-lm', '-o', exe], { encoding: 'utf8', windowsHide: true });
if (built.status !== 0) throw new Error(built.stderr || built.error?.message);
const result = spawnSync(exe, [], { encoding: 'utf8', windowsHide: true, timeout: 5000 });
process.stdout.write(result.stdout || '');
if (result.status !== 0) throw new Error(result.stderr || result.error?.message || 'completed episode startup failed');
