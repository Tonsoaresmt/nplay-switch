// Diagnostic only. Exit 0 means discovery defects reproduced, not approval.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
let player=readFileSync('source/player.c','utf8').replace(/\r\n/g,'\n');
if(!process.argv.includes('--baseline-053')) throw Error('Historical defect audit requires --baseline-053; use test_subtitle_discovery for current code.');
if(process.argv.includes('--baseline-053')){
 const git=process.platform==='win32'?'C:/Program Files/Git/cmd/git.exe':'git';
 const r=spawnSync(git,['show','cadecdb:source/player.c'],{encoding:'utf8',windowsHide:true});
 if(r.status!==0)throw Error(r.stderr);player=r.stdout.replace(/\r\n/g,'\n');
}
const begin=player.indexOf('    int native_nsub = nsub;');
const end=player.indexOf('    int acur = 0, best_stream =',begin);
if(begin<0||end<begin)throw Error('Review discovery boundaries');
mkdirSync('build',{recursive:true});
writeFileSync('build/audit_subtitle_discovery.inc',player.slice(begin,end));
const exe=resolve('build/audit_subtitle_discovery.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Itools/host-stubs','-Iinclude','-Ibuild','tools/audit_subtitle_discovery_host.c','source/hot_subtitles.c','source/hls_manifest.c','source/cJSON.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:10000});process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message);
console.log('AUDIT ONLY: real C discovery/parsers with simulated source/probe; no Dragon Ball source, physical Switch or production tested.');
