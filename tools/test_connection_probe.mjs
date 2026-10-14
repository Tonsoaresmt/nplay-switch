// Real loopback TLS (verified peer/hostname), not a measurement of the user's Wi-Fi.
import assert from 'node:assert/strict';
import {createServer} from 'node:https';
import {readFileSync,mkdirSync,existsSync} from 'node:fs';
import {spawn,spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const dir=resolve('build/connection-tls-fixture');mkdirSync(dir,{recursive:true});
const openssl=process.env.HOST_OPENSSL || 'C:/Program Files/Git/mingw64/bin/openssl.exe';
assert.ok(existsSync(openssl),'openssl required for local TLS fixture');
const cert=resolve(dir,'cert.pem'),key=resolve(dir,'key.pem'),other=resolve(dir,'untrusted.pem');
for(const [c,k] of [[cert,key],[other,resolve(dir,'untrusted-key.pem')]]){
 const r=spawnSync(openssl,['req','-x509','-newkey','rsa:2048','-nodes','-keyout',k,'-out',c,'-days','1',
   '-subj','/CN=127.0.0.1','-addext','subjectAltName=IP:127.0.0.1'],{encoding:'utf8',windowsHide:true,timeout:30000});
 assert.equal(r.status,0,r.stderr||r.error?.message);
}
const exe=resolve('build/connection_test_probe.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Itools/connection-stubs','-Iinclude',
 `-I${process.env.HOST_CURL_HEADERS||'C:/devkitPro/portlibs/switch/include'}`,'tools/connection_test_probe.c',
 process.env.HOST_CURL_LIBRARY||'C:/devkitPro/msys2/usr/bin/msys-curl-4.dll','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
assert.equal(r.status,0,r.stderr||r.error?.message);
let mode='good';
const server=createServer({cert:readFileSync(cert),key:readFileSync(key)},(req,res)=>{
 assert.equal(req.method,'GET');assert.equal(req.headers.authorization,undefined);assert.equal(req.headers.cookie,undefined);
 assert.equal(req.headers['x-profile-id'],undefined);assert.equal(req.headers['accept-encoding'],'identity');
 const download=req.url==='/download';
 res.writeHead(200,{'Content-Type':req.url==='/service'?'application/json':'application/octet-stream',
  'Content-Length':download?4194304:req.url==='/service'?2:0});
 if(!download){setTimeout(()=>res.end(req.url==='/service'?'{}':undefined),15);return;}
 let sent=0;const cap=mode==='partial'?1048576:4194304;
 const timer=setInterval(()=>{if(sent>=cap){clearInterval(timer);if(mode!=='partial')res.end();return;}
  res.write(Buffer.alloc(65536));sent+=65536;},12);
 res.on('close',()=>clearInterval(timer));
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
try{
 for(mode of ['good','partial','tls']){
  const p=spawn(exe,[`https://127.0.0.1:${server.address().port}`,mode==='tls'?other:cert,mode],{windowsHide:true});
  let output='';p.stdout.on('data',c=>output+=c);p.stderr.on('data',c=>output+=c);
  const code=await new Promise((ok,fail)=>{p.on('error',fail);p.on('close',ok);});
  console.log(`${mode}: ${output.trim()}`);assert.equal(code,0,output);
 }
}finally{server.closeAllConnections();await new Promise(r=>server.close(r));}
console.log('PASS real libcurl HTTPS: full 8MiB, partial 6s timeouts, TLS rejection, no credentials, bounded discard. Horizon untested.');
