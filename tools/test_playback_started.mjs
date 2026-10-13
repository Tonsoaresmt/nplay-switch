// Execute the real auxiliary worker with a deterministic presentation gate.
import {readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const p=readFileSync('source/player.c','utf8').replace(/\r\n/g,'\n');
const begin=p.indexOf('typedef struct {\n    int item_id;\n    SDL_atomic_t session_id;');
const end=p.indexOf('} PlaybackHeartbeat;',begin)+'} PlaybackHeartbeat;'.length;
const worker=p.indexOf('static int playback_heartbeat_thread(void *userdata) {');
const workerEnd=p.indexOf('int player_run(',worker);
if(begin<0 || worker<0 || end<begin || workerEnd<worker)throw Error('Worker boundaries changed');
mkdirSync('build',{recursive:true});
writeFileSync('build/playback_started.inc',p.slice(begin,end)+'\n'+p.slice(worker,workerEnd));
const exe=resolve('build/test_playback_started.exe');
const compile=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Itools/host-stubs','-Iinclude','-Ibuild','tools/playback_started_host.c','source/player_sync.c','-o',exe],{encoding:'utf8',windowsHide:true});
if(compile.status!==0)throw Error(compile.stderr||compile.error?.message);
const run=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:5000});
process.stdout.write(run.stdout||'');
if(run.status!==0)throw Error(run.stderr||run.error?.message);
