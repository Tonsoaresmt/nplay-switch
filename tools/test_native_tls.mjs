// Real patched FFmpeg/libnx callbacks; SDK, clocks, endpoints are deterministic stubs.
import {readFileSync,writeFileSync,mkdirSync,copyFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const net=readFileSync('source/net.c','utf8');
if(!net.includes('ff_nplay_tls_guard_version()') || !net.includes('diag_player_event("native-tls", stage, "result=0x%08X", result);'))throw Error('Native TLS integration/secure diagnostic boundary missing');
const fixture=readFileSync('tools/fixtures/devkitpro-ffmpeg-tls.patch');
if(createHash('sha256').update(fixture).digest('hex')!=='57ea7ec8ed26d13d3172d0fd589b7883d22f0c180d50b7434fcc73fb2b3ab7d7')throw Error('Pinned TLS fixture hash changed');
const lines=fixture.toString().split(/\r?\n/);
const i=lines.indexOf('+++ b/libavformat/tls_libnx.c');
if(i<0)throw Error('Missing TLS source');
const source=lines.slice(i+2).filter(l=>l.startsWith('+')).map(l=>l.slice(1)).join('\n')+'\n';
mkdirSync('build/native-tls/libavformat',{recursive:true});
writeFileSync('build/native-tls/libavformat/tls_libnx.c',source);
copyFileSync('tools/ffmpeg-libnx-tls-io.h','build/native-tls/libavformat/nplay_tls_io.h');
for(const patch of ['tools/ffmpeg-libnx-tls-hostname.patch','tools/ffmpeg-libnx-tls-io.patch']){
 const r=spawnSync(process.env.HOST_PATCH||'C:/Program Files/Git/usr/bin/patch.exe',['--batch','--fuzz=0','-p1','-d','build/native-tls','-i',resolve(patch)],{encoding:'utf8',windowsHide:true,timeout:10000});
 if(r.status!==0)throw Error(r.stdout+r.stderr);
}
const fixed=readFileSync('build/native-tls/libavformat/tls_libnx.c','utf8');
const a=fixed.indexOf('typedef struct TLSContext {'),b=fixed.indexOf('static int tls_get_file_handle(');
if(a<0||b<a)throw Error('Review native callback boundaries');
writeFileSync('build/native_tls_callbacks.inc',fixed.slice(a,b));
for(const mode of ['normal','missing-ca']) {
 const exe=resolve(`build/test_native_tls_${mode}.exe`);
 const defines=mode==='missing-ca'?['-DNPLAY_TEST_NO_TRUST']:[];
 let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',...defines,'-Ibuild','-Ibuild/native-tls/libavformat','tools/native_tls_host.c','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
 if(r.status!==0)throw Error(r.stderr||r.error?.message||`compiler ${r.status}`);
 r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:10000});
 process.stdout.write(r.stdout||'');if(r.status!==0)throw Error(r.stderr||r.error?.message||`native TLS ${r.status}`);
}
