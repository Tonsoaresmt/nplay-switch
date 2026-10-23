// Synthetic media + real FFmpeg CLI/HTTP and the actual C HLS trimmer.
// Not the integrated NRO, Horizon, NVTEGRA or production R2/TorBox.
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdirSync,readdirSync} from 'node:fs';
import {spawnSync,spawn} from 'node:child_process';
import {createServer} from 'node:http';
import {resolve,join,basename} from 'node:path';
const root=resolve('build/player-scenarios');mkdirSync(root,{recursive:true});
const ff=process.env.FFMPEG_EXE||'ffmpeg';
function run(cmd,args){const r=spawnSync(cmd,args,{encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:2*1024*1024});assert.equal(r.status,0,r.stderr||r.error?.message);return r;}
const trim=resolve('build/hls_trim_fixture.exe');
run(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','tools/hls_trim_fixture.c','source/hls_manifest.c','-o',trim]);
const profiles=[
 {id:'360p24',w:640,h:360,fps:'24',rate:44100,ch:1,sar:'1/1'},
 {id:'480p30',w:720,h:480,fps:'30000/1001',rate:48000,ch:2,sar:'8/9'},
 {id:'720p24',w:1280,h:720,fps:'24000/1001',rate:44100,ch:2,sar:'1/1'},
 {id:'1080p30',w:1920,h:1080,fps:'30',rate:48000,ch:2,sar:'1/1'},
 {id:'1080p60',w:1920,h:1080,fps:'60',rate:48000,ch:6,sar:'1/1'},
 {id:'portrait30',w:720,h:1280,fps:'30',rate:48000,ch:2,sar:'1/1'},
];
const assets=new Map(),positions=new Map();
for(const p of profiles){
 const dir=join(root,p.id);mkdirSync(dir,{recursive:true});
 run(ff,['-hide_banner','-loglevel','error','-y','-f','lavfi','-i',`testsrc2=s=${p.w}x${p.h}:r=${p.fps}`,
  '-f','lavfi','-i',`sine=frequency=660:sample_rate=${p.rate}`,'-t','12','-vf',`setsar=${p.sar}`,
  '-c:v','libx264','-threads','2','-preset','ultrafast','-crf','32','-maxrate','6M','-bufsize','12M','-g',String(Math.round(Number(p.fps.split('/')[0])/(Number(p.fps.split('/')[1])||1)*2)),
  '-sc_threshold','0','-c:a','aac','-ac',String(p.ch),'-metadata:s:a:0','language=por','-movflags','+faststart',join(dir,'range.mp4')]);
 for(const base of [2,6])run(ff,['-hide_banner','-loglevel','error','-y','-ss',String(base),'-i',join(dir,'range.mp4'),
  '-t','5','-map','0:v','-map','0:a','-c','copy','-movflags','empty_moov+default_base_moof+frag_keyframe',join(dir,`restart${base}.mp4`)]);
 run(ff,['-hide_banner','-loglevel','error','-y','-i',join(dir,'range.mp4'),'-map','0:v','-map','0:a','-c','copy',
  '-f','hls','-hls_time','2','-hls_list_size','0','-hls_playlist_type','vod','-hls_segment_type','fmp4',
  '-hls_fmp4_init_filename',join(dir,'init.mp4'),'-hls_segment_filename',join(dir,'seg%02d.m4s'),join(dir,'index.m3u8')]);
 writeFileSync(join(dir,'index.m3u8'),readFileSync(join(dir,'index.m3u8'),'utf8').replace(/#EXT-X-MAP:URI="[^"]+"/,'#EXT-X-MAP:URI="init.mp4"'));
 for(const file of readdirSync(dir)){
  const data=readFileSync(join(dir,file));assert(data.length<=32*1024*1024);
  assets.set(`/${p.id}/${file}`,data);
 }
 for(const target of [8,4,10]){
  const result=run(trim,[join(dir,'index.m3u8'),String(target)]);
  const [base,total]=result.stderr.trim().split(/\s+/).map(Number);
  assert(base<=target && target-base<2.2 && total>=11.9);
  positions.set(`/${p.id}/at${target}.m3u8`,{body:Buffer.from(result.stdout),base,target});
 }
 console.log(`GENERATED ${p.id}: ${p.w}x${p.h} fps=${p.fps} audio=${p.rate}/${p.ch} SAR=${p.sar}`);
}
let requests=0;const counts=new Map(),byteSeeks=[];
const server=createServer((req,res)=>{
 requests++;const url=new URL(req.url,'http://localhost');
 const mode=url.searchParams.get('mode')||'ok';const key=url.pathname+'?'+mode;
 const count=(counts.get(key)||0)+1;counts.set(key,count);
 if(['403','404','416'].includes(mode)){res.writeHead(Number(mode));res.end();return;}
 if(mode==='503'&&count===1){res.writeHead(503);res.end();return;}
 const positioned=positions.get(url.pathname);
 if(positioned){res.writeHead(200,{'Content-Type':'application/vnd.apple.mpegurl'});res.end(positioned.body);return;}
 let path=url.pathname;const isRemux=basename(path)==='remux';
 if(isRemux){
  assert(!req.headers.range,'restart remux must not request byte Range');
  path=path.replace(/remux$/,`restart${Number(url.searchParams.get('t'))>=6?6:2}.mp4`);
 }
 const data=assets.get(path);if(!data){res.writeHead(404);res.end();return;}
 let start=0,end=data.length-1;const range=req.headers.range;
 if(range&&mode!=='no-range'){
  const m=/^bytes=(\d+)-(\d*)$/.exec(range);assert(m);start=Number(m[1]);
  if(m[2])end=Math.min(end,Number(m[2]));if(start>=data.length){res.writeHead(416);res.end();return;}
  byteSeeks.push({path,start});
 }
 const partial=Boolean(range&&mode!=='no-range');
 const send=()=>{
  if(res.destroyed)return;
  res.writeHead(partial?206:200,{'Content-Type':path.endsWith('m3u8')?'application/vnd.apple.mpegurl':'video/mp4',
   ...(isRemux?{'Transfer-Encoding':'chunked'}:{'Content-Length':end-start+1}),...(mode==='no-range'?{}:{'Accept-Ranges':'bytes'}),
   ...(partial?{'Content-Range':`bytes ${start}-${end}/${data.length}`}:{})});
  if(mode==='drip'){
   let cursor=start;const timer=setInterval(()=>{
    if(res.destroyed||cursor>end){clearInterval(timer);if(!res.destroyed)res.end();return;}
    const next=Math.min(cursor+65536,end+1);res.write(data.subarray(cursor,next));cursor=next;
   },10);res.on('close',()=>clearInterval(timer));
  }else res.end(data.subarray(start,end+1));
 };
 if(mode==='delay')setTimeout(send,400);else send();
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));const base=`http://127.0.0.1:${server.address().port}`;
async function decode(path,before=[]){
 return new Promise((resolve,reject)=>{
  let output='',error='';let settled=false;
  const p=spawn(ff,['-hide_banner','-loglevel','info','-threads','2','-rw_timeout','3000000','-reconnect','1',
   '-reconnect_on_http_error','408,429,500,502,503,504','-reconnect_max_retries','2','-reconnect_delay_max','1',
   ...before,'-i',base+path,'-map','0:v:0','-map','0:a:0','-frames:v','3','-t','0.25','-vf','showinfo',
   '-progress','pipe:1','-f','null','-'],{windowsHide:true});
  p.stdout.on('data',b=>output=(output+b).slice(-65536));p.stderr.on('data',b=>error=(error+b).slice(-65536));
  const timer=setTimeout(()=>{if(!settled){settled=true;p.kill();reject(Error('scenario timeout '+path));}},15000);
  p.on('error',e=>{clearTimeout(timer);if(!settled){settled=true;reject(e);}});
  p.on('close',code=>{clearTimeout(timer);if(!settled){settled=true;resolve({code,output,error});}});
 });
}
let passed=0,expectedNoRangeChecksum='';
function success(r,p,label){assert.equal(r.code,0,label+' '+r.error);assert.match(r.output,/frame=3\b/,label);assert(r.error.includes(`s:${p.w}x${p.h}`),label+' decoded dimensions');passed++;}
try{
 for(const p of profiles){
  for(const target of [0,8,4]){
   const r=await decode(`/${p.id}/range.mp4`,['-ss',String(target),'-seekable','1']);success(r,p,`${p.id} Range ${target}`);
   if(p.id==='720p24'&&target===8)expectedNoRangeChecksum=/checksum:([A-F0-9]+)/.exec(r.error)?.[1];
  }
  assert(byteSeeks.some(r=>r.path===`/${p.id}/range.mp4`&&r.start>0));
  for(const target of [8,4,10])success(await decode(`/${p.id}/at${target}.m3u8`),p,`${p.id} actual positioned HLS ${target}`);
  for(const target of [8,4])success(await decode(`/${p.id}/remux?t=${target}`,['-seekable','0','-reconnect_streamed','0']),p,`${p.id} chunked-contract restart ${target}`);
  console.log(`PASS ${p.id}: initial/middle/back Range, 3 actual-C positioned HLS playlists, forward/back remux, decoded video/audio.`);
 }
 const p=profiles[2];
 for(const mode of ['delay','drip','503'])success(await decode(`/${p.id}/range.mp4?mode=${mode}`),p,mode);
 assert.equal(counts.get(`/${p.id}/range.mp4?503`),2);
 for(const code of ['403','404','416']){
  const r=await decode(`/${p.id}/range.mp4?mode=${code}`);assert.notEqual(r.code,0);assert.equal(counts.get(`/${p.id}/range.mp4?${code}`),1);passed++;
 }
 // A server ignoring Range must never be treated as proof of a successful
 // positioned open. This check describes FFmpeg CLI, not the NRO's supervisor.
 const noRange=await decode(`/${p.id}/range.mp4?mode=no-range`,['-ss','8','-seekable','1']);
 const noRangeChecksum=/checksum:([A-F0-9]+)/.exec(noRange.error)?.[1];
 if(noRange.code===0&&noRangeChecksum){assert(expectedNoRangeChecksum);assert.equal(noRangeChecksum,expectedNoRangeChecksum,'successful ignored-Range path must reach requested frame');}
 else {
  assert(!/frame=3\b/.test(noRange.output));
  const source=readFileSync('source/player.c','utf8');
  assert(source.includes('if (resume_preroll && reached_end)')&&source.includes('if (!ever_presented_frame)'), 'NRO must reject zero-frame completion');
 }
 console.log('Ignored Range result: '+(noRangeChecksum?'position verified by frame checksum':`no video frames (CLI exit=${noRange.code}; not accepted as playable)`));passed++;
 console.log(`SCENARIO MATRIX PASS: ${passed} real-media cases, ${profiles.length} profiles, ${requests} HTTP requests. No physical Switch/GPU/production validation.`);
 const result={passed,profiles:profiles.length,requests,cli:run(ff,['-version']).stdout.split('\n')[0],hardware:false};
 writeFileSync(join(root,'results.json'),JSON.stringify(result,null,2)+'\n');
}finally{server.closeAllConnections();await new Promise(r=>server.close(r));}
