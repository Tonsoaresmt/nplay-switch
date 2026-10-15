// Real FFmpeg CLI + localhost media, not Horizon/NVTEGRA or live TorBox/R2.
import assert from 'node:assert/strict';
import {readFileSync,mkdirSync} from 'node:fs';
import {spawnSync,spawn} from 'node:child_process';
import {createServer} from 'node:http';
import {resolve} from 'node:path';
const root=resolve('build/delivery-fixture');mkdirSync(root,{recursive:true});
const ff=process.env.FFMPEG_EXE||'ffmpeg';
function generate(args){const r=spawnSync(ff,['-hide_banner','-loglevel','error','-y',...args],{encoding:'utf8',windowsHide:true,timeout:60000});assert.equal(r.status,0,r.stderr||r.error?.message);}
generate(['-f','lavfi','-i','testsrc2=s=192x108:r=24','-f','lavfi','-i','sine=frequency=600:sample_rate=48000',
 '-t','32','-c:v','libx264','-preset','ultrafast','-g','48','-c:a','aac','-movflags','+faststart',`${root}/range.mp4`]);
for(const base of [4,18])generate(['-ss',String(base),'-i',`${root}/range.mp4`,'-t','8','-map','0:v','-map','0:a','-c','copy',
 '-movflags','empty_moov+default_base_moof+frag_keyframe',`${root}/restart-${base}.mp4`]);
const range=readFileSync(`${root}/range.mp4`),clips=new Map([4,18].map(n=>[n,readFileSync(`${root}/restart-${n}.mp4`)]));
let denied=0,temporary=0;const ranges=[],targets=[];
const server=createServer((req,res)=>{
 const url=new URL(req.url,'http://localhost');
 if(url.pathname==='/denied'){denied++;res.writeHead(403);res.end();return;}
 if(url.pathname==='/temporary'){
  temporary++;if(temporary===1){res.writeHead(503);res.end();return;}
 }
 if(url.pathname==='/remux'){
  const target=Number(url.searchParams.get('t')),base=target>=18?18:4;targets.push(target);
  assert.equal(req.headers.range,undefined,'restart remux cannot request byte Range');
  res.writeHead(200,{'Content-Type':'video/mp4','Transfer-Encoding':'chunked','Connection':'close'});
  const data=clips.get(base);for(let i=0;i<data.length;i+=4096)res.write(data.subarray(i,i+4096));res.end();return;
 }
 let start=0,end=range.length-1;const header=req.headers.range;
 if(header){ranges.push(header);const m=/^bytes=(\d+)-(\d*)$/.exec(header);assert(m);start=Number(m[1]);if(m[2])end=Math.min(end,Number(m[2]));}
 if(start>=range.length){res.writeHead(416);res.end();return;}
 res.writeHead(header?206:200,{'Content-Type':'video/mp4','Accept-Ranges':'bytes','Content-Length':end-start+1,
  ...(header?{'Content-Range':`bytes ${start}-${end}/${range.length}`}:{})});res.end(range.subarray(start,end+1));
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const base=`http://127.0.0.1:${server.address().port}`;
const source=readFileSync('source/player.c','utf8');
const match=/av_dict_set\(&open_opts, "reconnect_on_http_error", "([^"]+)"/.exec(source);assert(match);
assert(source.includes('"reconnect_streamed", sequential_stream ? "0" : "1"'));
const policy=['-reconnect','1','-reconnect_on_network_error','1','-reconnect_on_http_error',match[1],
 '-reconnect_max_retries','3','-reconnect_delay_max','1','-rw_timeout','3000000'];
function decode(path,before=[],after=[]){return new Promise((resolve,reject)=>{
 let output='',error='';const p=spawn(ff,['-hide_banner','-loglevel','error',...policy,...before,'-i',base+path,
  ...after,'-frames:v','3','-progress','pipe:1','-f','null','-'],{windowsHide:true});
 p.stdout.on('data',b=>output+=b);p.stderr.on('data',b=>error+=b);
 const timeout=setTimeout(()=>{p.kill();reject(Error('media fixture timed out'));},15000);
 p.on('error',reject);p.on('close',code=>{clearTimeout(timeout);resolve({code,output,error});});
});}
try{
 let r=await decode('/range',['-ss','20','-seekable','1']);assert.equal(r.code,0,r.error);assert.match(r.output,/frame=3\b/);
 assert(ranges.some(s=>Number(/^bytes=(\d+)/.exec(s)[1])>0),'MP4 seek must request a nonzero byte offset');
 r=await decode('/denied');assert.notEqual(r.code,0);assert.equal(denied,1,'permanent 403 must not reconnect');
 r=await decode('/temporary');assert.equal(r.code,0,r.error);assert.equal(temporary,2,'503 should recover with bounded retry');
 for(const target of [20,6]){
  r=await decode(`/remux?t=${target.toFixed(3)}`,['-seekable','0','-reconnect_streamed','0']);
  assert.equal(r.code,0,r.error);assert.match(r.output,/frame=3\b/);
 }
 assert.deepEqual(targets,[20,6]);
 console.log('PASS real FFmpeg/local HTTP: MP4 nonzero Range seek, permanent 403 single request, temporary 503 recovery, forward/back chunked restart clips decode frames without Range.');
 console.log('CLI='+spawnSync(ff,['-version'],{encoding:'utf8',windowsHide:true}).stdout.split('\n')[0]);
}finally{server.closeAllConnections();await new Promise(r=>server.close(r));}
