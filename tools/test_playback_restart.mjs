import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const exe=resolve('build/test_playback_restart.exe');
const cc=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Itools/host-stubs','-Iinclude','source/api.c','source/playback_restart.c','source/hot_subtitles.c','source/cJSON.c','tools/test_playback_restart.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(cc.status!==0)throw Error(cc.stderr||cc.error?.message);
const r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:5000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);process.stdout.write(r.stdout);
