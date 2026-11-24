// Real API C and real callback glue; HTTP/clock/cancel mocked, no live account.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const main=readFileSync('source/main.c','utf8');
const api=readFileSync('source/api.c','utf8');
if(!api.includes('cJSON_AddBoolToObject(request, "new_session", 1)'))throw Error('Admission can reuse a prior stopped SID');
const a=main.indexOf('static int on_player_stop('),b=main.indexOf('static int on_player_renew(',a);
if(a<0||b<a||/api_stop_playback\(|\/api\/stream\/%d\/stop/.test(main+api))throw Error('NRO still closes playback by item');
mkdirSync('build',{recursive:true});writeFileSync('build/session_stop_glue.inc',main.slice(a,b));
const exe=resolve('build/test_session_stop.exe');
for(const [cmd,args]of[[process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Itools/host-stubs','-Iinclude','-Ibuild','tools/session_stop_host.c','source/api.c','source/playback_restart.c','source/hot_subtitles.c','source/hls_manifest.c','source/cJSON.c','-lm','-o',exe]],[exe,[]]]){
 const r=spawnSync(cmd,args,{encoding:'utf8',windowsHide:true,timeout:30000});process.stdout.write(r.stdout||'');if(r.status!==0)throw Error(r.stderr||r.error?.message);
}
