// Real resolver UI/workers + api.c, pthreads; SDL rendering and HTTP simulated.
import {readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const main=readFileSync('source/main.c','utf8').replace(/\r\n/g,'\n');
function section(text,a,b){const s=text.indexOf(a),e=text.indexOf(b,s);if(s<0||e<s)throw Error(a);return text.slice(s,e);}
const start=main.lastIndexOf('typedef struct {',main.indexOf('} ResolvePoll;'));
let workers=main.slice(start,main.indexOf('// Um token JWT',start));
if(process.argv.includes('--baseline-progress')){
  const git=process.platform==='win32'?'C:/Program Files/Git/cmd/git.exe':'git';
  const r=spawnSync(git,['show','3d36eb6:source/main.c'],{encoding:'utf8',windowsHide:true});
  if(r.status!==0)throw Error(r.stderr);
  workers=workers.replace(section(main,'static int resolve_progress_thread(', 'static int resolve_open_thread('),
    section(r.stdout.replace(/\r\n/g,'\n'),'static int resolve_progress_thread(', 'static int resolve_open_thread('));
}
mkdirSync('build',{recursive:true});
writeFileSync('build/playback_resolution.inc',workers+'\n'+section(main,
  'static int resolve_open_once_with_animation(', '#define STABLE_TEXT'));
writeFileSync('build/playback_progress_guard.inc',section(main,
  '        double start = 0;', '        start = playback_resume_position(start, completed);'));
// Extract the real HTTP growth callback too: the descriptor's byte ceiling
// must work without Content-Length, not just be asserted by a fake server.
const net=readFileSync('source/net.c','utf8').replace(/\r\n/g,'\n');
writeFileSync('build/playback_resolution_write.inc',section(net,'static size_t write_cb(', 'struct file_download_ctx'));
writeFileSync('build/playback_request_timing.inc',section(net,
  'static _Thread_local NetRequestTiming g_request_timing;', 'long net_request_timeout_cancel('));
const exe=resolve('build/test_playback_resolution'+(process.platform==='win32'?'.exe':''));
const args=['-std=c11','-Wall','-Wextra','-Werror','-pthread','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
  '-Itools/host-stubs','-Iinclude','-Ibuild','tools/playback_resolution_host.c','source/api.c','source/hot_subtitles.c','source/cJSON.c','-lm','-o',exe];
let r=spawnSync(process.env.HOST_CC||'gcc',args,{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:30000});
process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message||`exit ${r.status}`);
if(!main.includes('if (cancelled || !playback_progress_valid(pr))'))throw Error('Unknown progress can still start from zero');
if(!net.includes('capture_request_timing(curl);') ||
   !readFileSync('source/diag.c','utf8').includes('dns=%u tcp=%u tls=%u first=%u'))throw Error('Network timing diagnostic not integrated');
console.log('No physical Switch/GPU/SSL test; timeout delays compressed, HTTP/SDL simulated.');
