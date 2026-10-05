import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
import {readFileSync,writeFileSync} from 'node:fs';
const store=readFileSync('source/store.c','utf8').replace(/\r\n/g,'\n');
const start=store.indexOf('int store_watchlater_reconcile('),end=store.indexOf('int store_media_list_remove(',start);
if(start<0||end<start)throw Error('Store test extraction boundary');
writeFileSync('build/media_list_wrappers.inc',store.slice(start,end));
const exe=resolve('build/test_media_list_sync.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Iinclude',
 '-Ibuild','tools/test_media_list_sync.c','source/media_list_sync.c','source/cJSON.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:10000});process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message);
