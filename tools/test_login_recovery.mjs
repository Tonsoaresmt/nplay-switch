// Actual catalog worker and accepted-result dispatch; HTTP/storage/SDL mocked.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const read=p=>readFileSync(p,'utf8').replace(/\r\n/g,'\n');
const current=read('source/main.c');
let main=current,worker=read('source/catalog_fetch.c');
if(process.argv.includes('--baseline-059')){
 const git=process.platform==='win32'?'C:/Program Files/Git/cmd/git.exe':'git';
 const historical=p=>{const r=spawnSync(git,['show',`d936f7a:${p}`],{encoding:'utf8',windowsHide:true});if(r.status!==0)throw Error(r.stderr);return r.stdout.replace(/\r\n/g,'\n');};
 main=historical('source/main.c');worker=historical('source/catalog_fetch.c');
}
const section=(s,a,b)=>{const i=s.indexOf(a),j=s.indexOf(b,i);if(i<0||j<i)throw Error(a);return s.slice(i,j);};
mkdirSync('build',{recursive:true});
writeFileSync('build/login_catalog_worker.inc',worker);
writeFileSync('build/login_clear_session.inc',section(current,'static void session_invalid_to_login(', '// O remux em tempo real'));
writeFileSync('build/login_catalog_dispatch.inc',section(main,'static void pump_catalog_fetch(void) {','    int applied = 0;')+'    reached_application++;\n}\n');
const exe=resolve('build/test_login_recovery.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-pthread',
 '-Itools/host-stubs','-Iinclude','-Ibuild','tools/login_recovery_host.c','source/cJSON.c','-o',exe],
 {encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:30000});
process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message||`exit ${r.status}`);
console.log('No authenticated production login or physical Switch/TLS handshake tested.');
