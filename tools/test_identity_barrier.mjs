// Real main barrier/glue with real pthread ownership; HTTP/SD/render mocked.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const p=readFileSync('source/main.c','utf8').replace(/\r\n/g,'\n');
function section(a,b){const i=p.indexOf(a),j=p.indexOf(b,i);if(i<0||j<i)throw Error(a);return p.slice(i,j);}
mkdirSync('build',{recursive:true});
writeFileSync('build/identity_barrier.inc',section('static void identity_reset_begin(void) {','// ------------------------------------------------------------- main'));
writeFileSync('build/identity_session.inc',section('static void session_invalid_to_login(', 'static void access_expired_to_login('));
if(!p.includes('if (g_identity_reset_pending) identity_reset_pump();\n        else {\n        pump_downloads();'))throw Error('Old result pumps not fenced during identity reset');
for(const signature of ['static void handle_button(int b) {','static void handle_touch_tap(int x, int y) {','static void draw_login(void) {']){
 if(!p.slice(p.indexOf(signature)+signature.length).trimStart().startsWith('if (g_identity_reset_pending)'))throw Error('Input/render not fenced: '+signature);
}
const exe=resolve('build/test_identity_barrier.exe');
for(const [cmd,args]of[[process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-pthread','-Itools/host-stubs','-Iinclude','-Ibuild','tools/identity_barrier_host.c','source/cJSON.c','-o',exe]],[exe,[]]]){
 const r=spawnSync(cmd,args,{encoding:'utf8',windowsHide:true,timeout:30000});if(r.status!==0)throw Error(r.stderr||r.error?.message);process.stdout.write(r.stdout||'');
}
