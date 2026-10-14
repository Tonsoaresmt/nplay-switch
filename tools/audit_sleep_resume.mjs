// Diagnostic only: reproduce existing behavior, not a release acceptance test.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const main=readFileSync('source/main.c','utf8').replace(/\r\n/g,'\n');
const start=main.indexOf('static int on_player_renew(');
const end=main.indexOf('static int on_player_fallback(',start);
if(start<0||end<start)throw Error('Review renewal callback boundaries');
mkdirSync('build',{recursive:true});
writeFileSync('build/sleep_resume_callback.inc',main.slice(start,end));
const exe=resolve('build/audit_sleep_resume.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Itools/host-stubs','-Iinclude','-Ibuild','source/api.c','source/hot_subtitles.c','source/cJSON.c','tools/audit_sleep_resume_host.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:5000});
process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message);
console.log('AUDIT ONLY: reproduces current session-recovery gap; does NOT certify physical sleep/resume or a fix.');
