// Positive regressions. libnx dispatch/network simulated, lifecycle/API C real.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
let main=readFileSync('source/main.c','utf8').replace(/\r\n/g,'\n');
if(process.argv.includes('--baseline-callback')){
 const git=process.platform==='win32'?'C:/Program Files/Git/cmd/git.exe':'git';
 const r=spawnSync(git,['show','3d36eb6:source/main.c'],{encoding:'utf8',windowsHide:true});
 if(r.status!==0)throw Error(r.stderr);main=r.stdout.replace(/\r\n/g,'\n');
}
const start=main.indexOf('static int on_player_renew('),end=main.indexOf('static int on_player_fallback(',start);
if(start<0||end<start)throw Error('Review renewal callback boundaries');
mkdirSync('build',{recursive:true});writeFileSync('build/sleep_resume_callback.inc',main.slice(start,end));
const exe=resolve('build/test_sleep_resume.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-D__SWITCH__','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Itools/sleep-stubs','-Itools/host-stubs','-Iinclude','-Ibuild','source/player_lifecycle.c','source/api.c','source/playback_restart.c','source/hot_subtitles.c','source/cJSON.c','tools/sleep_resume_host.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:10000});process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message);
console.log('No physical HOME/sleep, NVTEGRA, Wi-Fi or production session tested.');
const player=readFileSync('source/player.c','utf8').replace(/\r\n/g,'\n');
const worker=player.lastIndexOf('typedef struct {',player.indexOf('} PlayerRecoveryJob;'));
const workerEnd=player.indexOf('typedef struct {\n    AVPacket *packet;',worker);
if(worker<0||workerEnd<worker)throw Error('Review recovery worker boundaries');
writeFileSync('build/recovery_lifecycle.inc',player.slice(worker,workerEnd));
const uiExe=resolve('build/test_recovery_lifecycle.exe');
r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-pthread','-D__SWITCH__','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Itools/sleep-stubs','-Itools/host-stubs','-Iinclude','-Ibuild','source/player_lifecycle.c','source/api.c','source/playback_restart.c','source/hot_subtitles.c','source/cJSON.c','tools/recovery_lifecycle_host.c','-lm','-o',uiExe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(uiExe,[],{encoding:'utf8',windowsHide:true,timeout:20000});process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message);
