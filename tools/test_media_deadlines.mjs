// Actual scheduler/PCM/hold C with synthetic decoder, SDL device and clock.
// Real CLI-generated packet timestamps; this does not execute NVTEGRA/Horizon.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const p=readFileSync('source/player.c','utf8').replace(/\r\n/g,'\n');
const slice=(a,b,from=0)=>{const i=p.indexOf(a,from),j=p.indexOf(b,i);if(i<0||j<i)throw Error(a);return p.slice(i,j);};
mkdirSync('build',{recursive:true});
writeFileSync('build/av_resume_helper.inc',slice('static int player_queue_pcm(','static int create_audio_dec('));
writeFileSync('build/av_resume_queue.inc',slice('                        double audio_pts = ats != AV_NOPTS_VALUE','\n                    }\n                }\n            }\n        } else if (pkt->stream_index == vidx)'));
writeFileSync('build/av_resume_schedule.inc',slice('        int take = 0;','        int ret = take',p.indexOf('Uint32 audio_before_ms')));
writeFileSync('build/media_hold.inc',slice('                    if (delay > 0.02) {','                    if (delay > 0.001)',p.indexOf('double delay = vpts - master;')));
writeFileSync('build/media_redraw.inc',slice('                int redraw_wait =','                if (redraw_wait)',p.indexOf('        if (decoder_wait)')));
let host=readFileSync('tools/av_resume_host.c','utf8').split('static void matrix(void)')[0].replace('streams[16]','streams[1024]');
host+='#include "player_clock.h"\n#include "player_completion.h"\n';
host+=`
static void hold(double vpts){double delay=vpts;do{
#include "media_hold.inc"
}while(0);}
static void advance(int ms){unsigned bytes=(unsigned)(ms*bps/1000.0);if(bytes>queued_bytes)bytes=queued_bytes;queued_bytes-=bytes;model_time+=(int64_t)ms*1000;}
static int run_media(const int*kind,const double*pts,int n,double duration,int expect_vfr){
 reset(0);stream_count=n;terminal=AVERROR_EOF;eof_drain_stage=0;
 for(int i=0;i<n;i++){streams[i]=kind[i];packet_ids[i]=i;}
 int presented=0,iterations=0,drained=0,held=0;double last_video=0;
 while(++iterations<30000){
  int packet=-1;int rc=schedule((unsigned)(queued_bytes*1000.0/bps),0,&packet);
  if(scheduled_wait){
   cur_pos=video_frame_held?model_time/1e6-wall_start:player_audio_presented(cur_pos,audio_clock,queued_bytes,bps);
   // Model a nonzero 16ms vblank cost, using the actual redraw guard.
#include "media_redraw.inc"
   if(redraw_wait)advance(16);
   advance(8);continue;
  }
  if(rc==AVERROR_EOF){
   if(eof_drain_stage<2){if(eof_drain_stage++==0)drained++;continue;}
   cur_pos=player_audio_presented(cur_pos,audio_clock,queued_bytes,bps);
   if(queued_bytes<8192)break;
   advance(8);continue;
  }
  assert(rc==1&&running&&!playback_error);
  if(scheduled_drain){drained++;video_receive_pending=0;}
  else if(packet==1){feed(pts[taken_id],1024);assert(running&&!playback_error);}
  else {
   int id=scheduled_replay?frame->id:taken_id;
   double vpts=pts[id],now=model_time/1e6;
   double master=player_clock_master(now,vpts,audio_clock,queued_bytes/bps,queued_bytes>0,&wall_start,!presented);
   cur_pos=master;
   if(!scheduled_replay){frame->id=id;frame->has_data=1;hold(vpts-master);if(video_frame_held){held++;held_video_pts=vpts;advance(1);continue;}}
   // Replay must not present early, and must not lose the original frame ID.
   assert(vpts-master<0.025&&master-vpts<0.06);
   presented++;last_video=vpts;video_receive_pending=0;
  }
  assert(queued_bytes<=bps*PLAYER_PCM_LIMIT_SECONDS);
  advance(1);
 }
 if(!(iterations<30000&&running&&!playback_error&&!stream_count&&!video_frame_held&&drained==1))fprintf(stderr,"state loops%d running%d error%d streams%d held%d drained%d PCM%u pos%f time%f\\n",iterations,running,playback_error,stream_count,video_frame_held,drained,queued_bytes,cur_pos,model_time/1e6);
 assert(iterations<30000&&running&&!playback_error&&!stream_count&&!video_frame_held&&drained==1);
 assert(player_eof_complete(cur_pos,duration,0));
 assert(!player_eof_complete(cur_pos,duration+100,0)); // Tail must not excuse truncation.
 if(expect_vfr)assert(presented==3&&held>=2&&last_video==8&&model_time>=8000000);
 else assert(presented>20&&cur_pos>7.8&&max_audio_queue<230000);
 av_frame_free(&pending_video);assert(!frames_allocated);
 printf("PASS media scheduler: video=%d holds=%d position=%.3f time=%.3f maxPCM=%u iterations=%d\\n",presented,held,cur_pos,model_time/1e6,max_audio_queue,iterations);
 return 0;
}
`;
const cmd=(name,args)=>{const r=spawnSync(name,args,{encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:4*1024*1024});if(r.status!==0)throw Error(r.stderr||r.error?.message);return r.stdout;};
const fixtures=[];
for(const type of ['tail','vfr']){
 const file=`build/deadline-${type}.mp4`,vfr=type==='vfr';
 cmd('ffmpeg',['-v','error','-y','-f','lavfi','-i',`testsrc2=s=320x180:r=24:d=${vfr?9:2}`,'-f','lavfi','-i',`sine=sample_rate=48000:duration=${vfr?9:8}`,...(vfr?['-vf',"select='not(mod(n,96))'",'-fps_mode','vfr']:[]),'-c:v','libx264','-preset','ultrafast','-c:a','aac','-g','24','-movflags',vfr?'+faststart':'+frag_keyframe+empty_moov+default_base_moof',file]);
 const packets=JSON.parse(cmd('ffprobe',['-v','error','-show_packets','-show_entries','packet=stream_index,pts_time,size','-of','json',file])).packets;
 if(packets.length>512||packets.reduce((n,p)=>n+Number(p.size),0)>4*1024*1024)throw Error('Fixture exceeds existing demux budgets');
 fixtures.push({vfr,packets});
 const i=fixtures.length;
 host+=`static const int kind${i}[]={${packets.map(p=>p.stream_index)}};\nstatic const double pts${i}[]={${packets.map(p=>Number(p.pts_time))}};\n`;
}
host+=`int main(void){
 (void)SDL_GetError;
 run_media(kind1,pts1,sizeof(kind1)/sizeof(*kind1),8,0);
 run_media(kind2,pts2,sizeof(kind2)/sizeof(*kind2),9,1);
 // No-audio/starved input still waits on the frame deadline, never EAGAIN.
 reset(0);stream_count=0;terminal=0;frame->id=42;frame->has_data=1;hold(4);int packet=-1;
 assert(schedule(0,0,&packet)==0&&scheduled_wait&&video_frame_held);
 stream_count=1;streams[0]=2; // Native subtitle still processed while video is held.
 assert(schedule(1200,0,&packet)==1&&packet==2&&video_frame_held&&!stream_count);
 model_time=4000000;buffering_since=1;
 assert(schedule(0,0,&packet)==1&&scheduled_replay&&frame->id==42&&!video_frame_held);
 // Seeking invalidates a held generation; OOM fails closed without losing ownership.
 reset(600);assert(!pending_video&&!video_frame_held&&!video_receive_pending&&!frames_allocated);
 frame_oom=1;hold(4);assert(!running&&playback_error==-4&&!pending_video);frame_oom=0;
 reset(0);assert(!frames_allocated);
 assert(player_audio_presented(10,8,0,bps)==10&&player_audio_presented(1,NAN,0,bps)==1);
 puts("PASS future-frame ownership, starved deadline, seek reset, OOM and audible EOF guard. Device/decoder/clock simulated; packets from real FFmpeg CLI.");return 0;}
`;
writeFileSync('build/test_media_deadlines.c',host);
const exe=resolve('build/test_media_deadlines.exe');
cmd(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','-Ibuild','build/test_media_deadlines.c','source/player_clock.c','-lm','-o',exe]);
process.stdout.write(cmd(exe,[]));
// Every seek/track-generation reset must release the extra frame reference.
const resets=[...p.matchAll(/player_audio_timeline_reset\(&audio_timing,/g)];
if(resets.length!==5||resets.some(m=>!p.slice(m.index,m.index+220).includes('av_frame_free(&pending_video); video_frame_held = video_receive_pending = 0;')))throw Error('Generation retains stale held frame');
