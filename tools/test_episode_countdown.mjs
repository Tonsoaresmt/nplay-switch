import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const main=readFileSync('source/main.c','utf8').replace(/\r\n/g,'\n');
const start=main.indexOf('static int prompt_next_episode(cJSON *episode, cJSON *series) {');
const end=main.indexOf('static void fetch_episode_context(',start);
if(start<0||end<start)throw Error('Review native countdown extraction');
mkdirSync('build',{recursive:true});writeFileSync('build/episode_countdown_actual.inc',main.slice(start,end));
const exe=resolve('build/test_episode_countdown.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','-Ibuild','-Itools/host-stubs',
 'tools/episode_countdown_host.c','source/cJSON.c','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:5000});process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message||`exit ${r.status}`);
