#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "player_audio_timeline.h"
typedef uint32_t Uint32;
typedef int SDL_AudioDeviceID;
typedef struct {int num,den;} AVRational;
#define AV_NOPTS_VALUE INT64_MIN
static const int OCH=2,ORATE=48000,adev=1;
static const double bps=192000;
static int resume_preroll,running=1,playback_error,audio_switch_pending,subtitle_switch_pending;
static unsigned queued_bytes,max_audio_queue,audio_queue_failures,max_track_switch_ms,audio_switch_started;
static double resume_target=3000,audio_clock,cur_pos=3000,timeline_origin;
static struct {int force_loading;const char*headline;} open_watch;
static AVRational atb={1,48000};
static unsigned char pcm[48000*4];
static unsigned char*audio_buf=pcm;
static double input_pts,head_pts,last_end,gap_seconds;
static int injected_delay,fail_queue;
static int queued_calls;
static PlayerAudioTimeline audio_timing;
static double av_q2d(AVRational q){return (double)q.num/q.den;}
static unsigned SDL_GetQueuedAudioSize(int d){(void)d;return queued_bytes;}
static int SDL_QueueAudio(int d,const void*data,unsigned bytes){(void)d;
 if(fail_queue)return -1;
 uintptr_t ptr=(uintptr_t)data,first=(uintptr_t)pcm;
 int is_pcm=ptr>=first&&ptr<first+sizeof(pcm);
 double pts=is_pcm?input_pts+(ptr-first)/(double)(OCH*2*ORATE):
             queued_calls?last_end:fmax(audio_timing.floor,cur_pos);
 if(!queued_calls)head_pts=pts;
 else if(pts>last_end+1.0/ORATE)gap_seconds+=pts-last_end;
 last_end=pts+bytes/bps;queued_bytes+=bytes;queued_calls++;return 0;
}
static unsigned SDL_GetTicks(void){return 100;}
static const char*SDL_GetError(void){return "fixture";}
static void diag_player_event(const char*a,const char*b,const char*c,...){(void)a;(void)b;(void)c;}
static void player_error_message(const char*s){(void)s;}
#include "av_resume_helper.inc"
static void feed(double pts,int samples){
 input_pts=isfinite(pts)?pts-timeline_origin-injected_delay/(double)ORATE:audio_timing.end;
 int64_t ats=isfinite(pts)?(int64_t)llround(pts*48000):AV_NOPTS_VALUE;
 int n=samples;int64_t audio_resample_delay=injected_delay;
 (void)audio_resample_delay;
 for(int once=0;once<1;once++){
#include "av_resume_queue.inc"
 }
}
static int streams[16],stream_count,terminal;
static int demux_worker_terminal(int*w){(void)w;return terminal;}
static int demux_worker_take_stream(int*w,int stream,int*pkt,Uint32*ms){(void)w;(void)ms;
 for(int i=0;i<stream_count;i++)if(streams[i]==stream){*pkt=stream;memmove(streams+i,streams+i+1,(stream_count-i-1)*sizeof(*streams));stream_count--;return 1;}
 return 0;}
static int demux_worker_take(int*w,int*pkt,Uint32*ms){(void)w;(void)ms;if(!stream_count)return terminal;
 *pkt=streams[0];memmove(streams,streams+1,--stream_count*sizeof(*streams));return 1;}
static int demux_buffer_prefer_audio(unsigned ms,int paused){return !paused&&ms<1000;}
static int schedule(unsigned audio_before_ms,int preroll,int*pkt){
 int demux=0,aidx=1,vidx=0,actx=1,paused=0,refill=0;Uint32 read_ms=0;
 (void)vidx; // The published .57 scheduler had no video-first branch.
 resume_preroll=preroll;
#include "av_resume_schedule.inc"
 return take;
}
static void reset(double target){
 queued_bytes=queued_calls=max_audio_queue=audio_queue_failures=0;
 gap_seconds=head_pts=last_end=timeline_origin=0;injected_delay=fail_queue=0;
 cur_pos=resume_target=audio_clock=target;running=1;playback_error=0;
 player_audio_timeline_reset(&audio_timing,target);
}
static void matrix(void){
 for(int paused=0;paused<2;paused++)for(int presented=0;presented<2;presented++)for(int preroll=0;preroll<2;preroll++)
  assert(player_audio_ready(paused,presented,preroll)==(!paused&&presented&&!preroll));
 reset(3000);resume_preroll=0;feed(2999.95,4800);
 assert(queued_bytes==9600&&fabs(head_pts-3000)<1e-6&&fabs(last_end-3000.05)<1e-6);
 feed(3000.04,960);assert(fabs(last_end-3000.06)<1e-6&&gap_seconds<1e-6);
 unsigned before=queued_bytes;feed(2990,960);assert(queued_bytes==before);
 reset(3000);feed(3000.4,960);assert(fabs(head_pts-3000)<1e-6&&fabs(last_end-3000.42)<1e-6);
 assert(audio_timing.padded==19200&&fabs(audio_clock-queued_bytes/bps-head_pts)<1e-6);
 reset(600);timeline_origin=-598;feed(2,960);assert(fabs(head_pts-600)<1e-6&&fabs(audio_clock-600.02)<1e-6);
 reset(600);feed(NAN,960);assert(fabs(audio_clock-600.02)<1e-6);
 reset(600);injected_delay=16;feed(600,960);assert(audio_timing.trimmed==16&&fabs(head_pts-600)<1e-6);
 reset(600);cur_pos=600.5;feed(600,960);assert(!queued_bytes);feed(600.5,960);assert(fabs(head_pts-cur_pos)<1e-6);
 reset(600);feed(603,960);assert(playback_error==-5&&!running&&!queued_bytes&&audio_clock==600);
 reset(600);fail_queue=1;feed(600,960);assert(playback_error==-4&&!running&&!audio_timing.valid);
 PlayerAudioPlan plan;PlayerAudioTimeline s;player_audio_timeline_reset(&s,0);
 assert(player_audio_plan(&s,0,192001,48000,2,0,0,&plan)==-2);
 for(int rate=8000;rate<=96000;rate+=8000)for(int count=1;count<2048;count+=127){
  player_audio_timeline_reset(&s,100);int rc=player_audio_plan(&s,100-0.005,count,rate,2,100,0,&plan);
  assert(rc==0||rc==1);if(rc==1){assert(plan.skip>=0&&plan.skip<count&&plan.count==count-plan.skip&&plan.silence>=0&&plan.end>=100);}
 }
 for(int group=0;group<4;group++)for(int jump=0;jump<100;jump++){
  double target=jump%3==0?0:jump%3==1?600:3000;
  reset(target);for(int i=0;i<100;i++)feed(target+i*0.02,960);
  assert(running&&queued_bytes==384000&&fabs(gap_seconds)<1e-6&&fabs(audio_clock-queued_bytes/bps-target)<1e-6);
 }
 for(int i=0;i<10;i++)streams[i]=1;
 streams[10]=0;stream_count=11;terminal=0;int pkt=-1;
 assert(schedule(400,1,&pkt)==1&&pkt==0&&stream_count==10); // Video past queued audio, no audio dropped.
 assert(schedule(400,1,&pkt)==0&&stream_count==10); // Wait for video, retain compressed audio.
 terminal=-1;assert(schedule(400,1,&pkt)==1&&pkt==1&&stream_count==9); // EOF can drain.
 stream_count=2;streams[0]=0;streams[1]=1;terminal=0;
 assert(schedule(100,0,&pkt)==1&&pkt==1&&stream_count==1);
 assert(schedule(1200,0,&pkt)==1&&pkt==0&&!stream_count);
 puts("PASS actual PCM/scheduling C: 400 seek epochs/40000 blocks, trim/overlap, delay/padding, late packets, nonzero origin, missing PTS, resampler delay, SDL failure, gap/budget rejection, video-first backpressure and EOF drain.");
}
static int trace(const char*path){
 FILE*f=fopen(path,"rb");if(!f)return 2;
 double target,origin;if(fscanf(f,"%lf %lf",&target,&origin)!=2){fclose(f);return 3;}
 reset(target);timeline_origin=origin;resume_preroll=1;
 double pts;int n,frames=0,accepted=0;double first_head=NAN;
 while(fscanf(f,"%lf %d",&pts,&n)==2){
  assert(++frames<=10000&&n>0&&n<=48000);
  unsigned before=queued_bytes;feed(pts,n);assert(running&&!playback_error);
  if(queued_bytes>before){accepted++;if(!isfinite(first_head))first_head=head_pts;}
  if(queued_bytes>=96000)resume_preroll=0;
  if(!resume_preroll){unsigned consumed=(unsigned)n*4u;if(consumed>queued_bytes)consumed=queued_bytes;queued_bytes-=consumed;head_pts+=consumed/bps;}
 }
 fclose(f);assert(frames>100&&accepted>20&&first_head>=target-1.0/ORATE);
 assert(fabs(gap_seconds)<0.002&&fabs(audio_clock-queued_bytes/bps-head_pts)<0.002);
 printf("PASS real decoded PCM timestamps -> actual C queue: origin=%.3f target=%.3f frames=%d accepted=%d gap=%.6f\n",origin,target,frames,accepted,gap_seconds);
 return 0;
}
int main(int argc,char**argv){setvbuf(stdout,NULL,_IONBF,0);(void)argc;(void)cur_pos;
 (void)SDL_GetError;(void)resume_preroll;(void)demux_worker_terminal;
 player_audio_timeline_reset(&audio_timing,resume_target);
 if(!strcmp(argv[1],"trace"))return argc==3?trace(argv[2]):2;
 if(!strcmp(argv[1],"matrix")){matrix();return 0;}
 if(!strcmp(argv[1],"late")){
  resume_preroll=0;feed(2999,4800);
  printf("Late audio after video target: queue=%u head=%.3f floor=%.3f\n",queued_bytes,head_pts,resume_target);
  assert(!queued_bytes);return 0;
 }
 resume_preroll=1;
 for(int i=0;i<100;i++)feed(3000+i*0.02,960);
 printf("Preroll audio: stored=%.3fs expected=2.000s\n",queued_bytes/bps);
 resume_preroll=0;feed(3002,960);
 printf("PCM gap after first video=%.3fs; queue-head=%.3f audible-estimate=%.3f\n",gap_seconds,head_pts,audio_clock-queued_bytes/bps);
 assert(fabs(gap_seconds)<1.0/ORATE && fabs(queued_bytes/bps-2.02)<1.0/ORATE);
 return 0;
}
