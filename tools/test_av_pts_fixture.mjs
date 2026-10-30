// Real AAC decode/resampling timestamps feed the app's actual PCM queue code.
// Does not execute SDL hardware or the NRO decoder/presentation pipeline.
import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve,join} from 'node:path';
const ff=process.env.FFMPEG_EXE||'ffmpeg',root=resolve('build/av-pts-fixture');mkdirSync(root,{recursive:true});
function run(cmd,args){const r=spawnSync(cmd,args,{encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:2*1024*1024});assert.equal(r.status,0,r.stderr||r.error?.message);return r;}
run(process.execPath,['tools/test_av_resume.mjs']);
let total=0;
for(const [rate,origin,offset] of [[44100,0,0],[48000,0,0.12],[44100,37,0],[48000,37,0.12]]){
 const name=`${rate}-${origin}-${offset}`,file=join(root,name+'.mp4');
 run(ff,['-hide_banner','-loglevel','error','-y','-f','lavfi','-i','testsrc2=s=320x180:r=24:d=6',
  '-itsoffset',String(offset),'-f','lavfi','-i',`sine=frequency=700:sample_rate=${rate}:duration=6`,
  '-map','0:v','-map','1:a','-c:v','libx264','-threads','2','-preset','ultrafast','-c:a','aac',
  '-output_ts_offset',String(origin),'-movflags','+faststart',file]);
 const r=run(ff,['-hide_banner','-loglevel','info','-copyts','-i',file,'-map','0:a:0','-af','aresample=48000,ashowinfo','-f','null','-']);
 const frames=[...r.stderr.matchAll(/pts_time:([\d.\-]+).*?nb_samples:(\d+)/g)];assert(frames.length>100);
 const trace=join(root,name+'.txt');
 writeFileSync(trace,'2.5 '+origin+'\n'+frames.map(m=>m[1]+' '+m[2]).join('\n')+'\n');
 const result=run(resolve('build/test_av_resume.exe'),['trace',trace]);process.stdout.write(result.stdout);total+=frames.length;
}
console.log(`AV PTS FIXTURE PASS: 4 files / ${total} actual AAC/resampled blocks, nonzero origin and audio offset; SDL queue/device simulated.`);
