// Compile the actual Switch-only CA callback and common curl configuration.
// Native SSL service is stubbed: this does NOT claim a Horizon handshake test.
import {readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const net=readFileSync('source/net.c','utf8').replace(/\r\n/g,'\n');
const section=(a,b)=>{const s=net.indexOf(a),e=net.indexOf(b,s);if(s<0||e<s)throw Error(a);return net.slice(s,e);};
mkdirSync('build',{recursive:true});
writeFileSync('build/tls_ca.inc',section('#ifdef __SWITCH__\n// devkitPro','static void share_lock('));
writeFileSync('build/tls_config.inc',section('void net_configure_curl_isolated(', 'void net_configure_curl(CURL'));
const ca=readFileSync('data/cacert.bin');
writeFileSync('build/tls_bundle.inc',`static const unsigned char cacert_bin[]={${Array.from(ca).join(',')}};\nstatic const unsigned int cacert_bin_size=sizeof(cacert_bin);\n`);
for(const marker of ['prepare_ca_memory();','free(g_ca_pem);','net_configure_curl_isolated(curl);'])
 if(!net.includes(marker))throw Error('Missing integration '+marker);
const exe=resolve('build/test_tls_ca.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-pthread','-Ibuild','tools/test_tls_ca.c','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:30000});
process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message||`exit ${r.status}`);
