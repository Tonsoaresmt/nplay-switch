// Actual diagnostic transport/model/UI; curl/network/GPU simulated, real pthreads.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const read=p=>readFileSync(p,'utf8').replace(/\r\n/g,'\n');
const main=read('source/main.c'), test=read('source/connection_test.c');
mkdirSync('build',{recursive:true});
const start=main.indexOf('static const char *const SETTINGS_NAV[]'),end=main.indexOf('static void open_profile_editor(',start);
if(start<0||end<start)throw Error('Settings extraction boundaries changed');
writeFileSync('build/connection_settings.inc',main.slice(start,end));
const ui=read('source/connection_test_ui.c');
const worker=ui.indexOf('static int connection_worker('),endWorker=ui.indexOf('static const SDL_Rect CONNECTION_BUTTON',worker);
writeFileSync('build/connection_ui.inc',ui.slice(worker,endWorker)+ui.slice(endWorker));
for(const marker of ['connection_test_show(gRen, BASE, &g_running);',
 'settings_row_y(g_settings_section, i)', 'settings_row_at(g_settings_section, settings_row_count(), x, y)'])
 if(!main.includes(marker))throw Error('Missing settings integration: '+marker);
if(test.includes('g_token')||test.includes('api_send')||test.includes('fopen(')||test.includes('SSL_VERIFYPEER, 0'))throw Error('Credential/write/security regression');
const exe=resolve('build/test_connection_test'+(process.platform==='win32'?'.exe':''));
const args=['-std=c11','-Wall','-Wextra','-Werror','-pthread','-Itools/connection-stubs','-Iinclude','-Isource','-Ibuild',
 `-I${process.env.HOST_CURL_HEADERS||'C:/devkitPro/portlibs/switch/include'}`,
 'tools/connection_test_host.c','source/connection_test.c','-lm','-o',exe];
let r=spawnSync(process.env.HOST_CC||'gcc',args,{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:30000});
process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message||`exit ${r.status}`);
console.log('No Horizon/physical Wi-Fi/GPU validation; simulated endpoints and time, real worker lifetime.');
