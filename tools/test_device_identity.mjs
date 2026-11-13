import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const read=p=>readFileSync(p,'utf8').replace(/\r\n/g,'\n');
const main=read('source/main.c'),store=read('source/store.c');
const extract=(s,a,b)=>{const i=s.indexOf(a),j=s.indexOf(b,i);if(i<0||j<i)throw Error(a);return s.slice(i,j);};
mkdirSync('build/device-identity-store',{recursive:true});
writeFileSync('build/device_identity_store.inc',extract(store,'static int device_id_valid(', 'static void trim_line('));
writeFileSync('build/device_identity_ensure.inc',extract(main,'static int ensure_device_identity(', 'static void pair_set_stage('));
for(const marker of ['ensure_device_identity(fingerprint, sizeof(fingerprint)) != 0','device_fingerprint','g_pair.fingerprint'])
 if(!main.includes(marker))throw Error(marker);
if((main.match(/ensure_device_identity\(fingerprint, sizeof\(fingerprint\)\)/g)||[]).length!==2)throw Error('QR/password must share stable identity');
const exe=resolve('build/test_device_identity.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Ibuild','tools/device_identity_host.c','-o',exe],
 {encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{cwd:resolve('build/device-identity-store'),encoding:'utf8',windowsHide:true,timeout:10000});
process.stdout.write(r.stdout||'');if(r.status!==0)throw Error(r.stderr||r.error?.message||`exit ${r.status}`);
