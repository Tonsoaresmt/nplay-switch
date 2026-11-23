// Real entry wrapper; resolution/player/UI are controlled stubs, not hardware.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const main=readFileSync('source/main.c','utf8').replace(/\r\n/g,'\n');
const marker='int resolve_and_play_details(int itemId, const char *title, const char *subtitle,\n                             const char *overview, const char *next_title, int has_next) {';
const start=main.indexOf(marker),end=main.indexOf('\nstatic int resolve_and_play_resolved(',start);
if(start<0||end<start)throw Error('Review actual entry wrapper boundaries');
mkdirSync('build',{recursive:true});writeFileSync('build/playback_entry_actual.inc',main.slice(start,end));
const exe=resolve('build/test_playback_entry.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','-Ibuild','-Itools/host-stubs',
 'tools/playback_entry_host.c','source/cJSON.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:10000});process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message||`exit ${r.status}`);
if(!main.includes('if (run_rc < 0 && res.reason != EXIT_REASON_USER)'))throw Error('Opening cancellation is classified as player failure');
