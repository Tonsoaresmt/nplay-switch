// --reproduce-061: exit0 means published defects REPRODUCED, not approval.
// --verify-current: corrected behavior assertions, suitable as regression gate.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const verify=process.argv.includes('--verify-current');
if(!verify&&!process.argv.includes('--reproduce-061'))throw Error('Use --reproduce-061 (negative) or --verify-current (regression)');
const git='C:/Program Files/Git/cmd/git.exe';
const r=spawnSync(git,['show','e684b28:source/player.c'],{encoding:'utf8',windowsHide:true});
if(r.status!==0)throw Error(r.stderr);
const player=(verify?readFileSync('source/player.c','utf8'):r.stdout).replace(/\r\n/g,'\n');
const part=(a,b,from=0)=>{const i=player.indexOf(a,from),j=player.indexOf(b,i);if(i<0||j<i)throw Error(a);return player.slice(i,j);};
mkdirSync('build',{recursive:true});
writeFileSync('build/deep_sub_auto.inc',part('    if (!sub_chosen && naud > 0) {',verify?'    char subtitle_auto_language[8]':'    int aidx = naud ? aidxs[acur] : -1;'));
writeFileSync('build/deep_audio_metadata.inc',part('    for (int i = 0; i < naud_ui; i++) {','    const char *saved_audio',player.indexOf('int audio_from_master =')));
writeFileSync('build/deep_discovery_retry.inc',part('static int subtitle_discovery_retryable(', 'static int progressive_subtitle_block('));
writeFileSync('build/deep_discovery_apply.inc',part('        if (subtitle_discovery_poll(discovery)) {','        if (!discovery && discovery_retry'));
const run=(name,sources)=>{
 const exe=resolve(`build/audit_deep_${name}.exe`);
 if(!verify)sources=sources.map(p=>{
  if(!p.startsWith('source/'))return p;
  const old=spawnSync(git,['show',`e684b28:${p}`],{encoding:'utf8',windowsHide:true});
  if(old.status!==0)throw Error(old.stderr);const saved=`build/deep_061_${p.split('/').pop()}`;writeFileSync(saved,old.stdout);return saved;
 });
 let x=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror',...(verify?['-DCORRECTED=1']:[]),'-Iinclude','-Itools/host-stubs','-Ibuild',...sources,'-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
 if(x.status!==0)throw Error(x.stderr||x.error?.message);
 x=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:15000});process.stdout.write(x.stdout||'');
 if(x.status!==0)throw Error(x.stderr||x.error?.message||name);
};
run('tracks',['tools/deep_player_tracks_host.c','source/audio_policy.c','source/subtitle_catalog.c','source/subtitle_choice.c','source/hot_subtitles.c','source/hls_manifest.c','source/cJSON.c']);
run('invariants',['tools/stress_track_policy.c','source/audio_policy.c','source/subtitle_store.c','source/subtitle_queue.c']);
const helper=part('static int player_queue_pcm(', 'static int create_audio_dec(');
writeFileSync('build/av_resume_helper.inc',helper);
writeFileSync('build/av_resume_queue.inc',part('                        double audio_pts = ats != AV_NOPTS_VALUE','\n                    }\n                }\n            }\n        } else if (pkt->stream_index == vidx)'));
writeFileSync('build/av_resume_schedule.inc',part('        int take = 0;','        int ret = take',player.indexOf('Uint32 audio_before_ms')));
let host=readFileSync('tools/av_resume_host.c','utf8').replace(/\r\n/g,'\n').split('int main(int argc,char**argv)')[0];
if(!verify)host='#define LEGACY_SCHEDULER 1\n'+host;
host=host.replace('static int streams[16],stream_count,terminal;','static int streams[1024],stream_count,terminal;');
host+=`\nint main(void){
 (void)matrix;(void)trace;(void)SDL_GetError;
 reset(600);resume_preroll=1;terminal=-541478725;
 for(int i=0;i<300;i++)streams[i]=1;
 streams[300]=0;stream_count=301;int packet=-1,consumed=0;
 while(running&&consumed<300){
  assert(schedule((unsigned)(queued_bytes*1000.0/bps),1,&packet)==1);
#ifdef CORRECTED
  if(packet==0){
   assert(consumed<188&&running&&!playback_error&&stream_count==300-consumed);
   puts("PASS terminal EOF preserves video-first before PCM cap, compressed audio retained.");return 0;
  }
#endif
  assert(packet==1);feed(600+consumed*1024.0/48000,1024);consumed++;
 }
#ifdef CORRECTED
 assert(!"terminal video was not selected");
#else
 assert(!running&&playback_error==-5&&consumed<=202&&stream_count>0);
 assert(streams[stream_count-1]==0&&queued_bytes<=4*192000u);
 printf("REPRODUCED terminal/preroll burst: %d AAC-sized blocks before video, PCM cap hit at %.2fs; buffered video still pending. Queue is bounded, failure/reconnect not OOM. Scheduling/PCM C real, packet/SDL/time simulated.\\n",consumed,queued_bytes/bps);
#endif
 return 0;
}\n`;
writeFileSync('build/deep_audio_terminal.c',host);run('terminal',['build/deep_audio_terminal.c']);
if(process.argv.includes('--media-fixture')){
 const file='build/audit-audio-first-061.mp4';
 let media=spawnSync('ffmpeg',['-v','error','-f','lavfi','-i','sine=frequency=440:sample_rate=48000:duration=6.4',
  '-f','lavfi','-i','testsrc2=size=320x180:rate=24:duration=6.4','-map','0:a:0','-map','1:v:0',
  '-c:a','aac','-b:a','128k','-c:v','libx264','-preset','ultrafast','-g','10000','-pix_fmt','yuv420p',
  '-movflags','+frag_keyframe+empty_moov+default_base_moof','-frag_duration','6400000','-y',file],
  {encoding:'utf8',windowsHide:true,timeout:30000});
 if(media.status!==0)throw Error(media.stderr||media.error?.message);
 const options=['-v','error','-show_packets','-show_entries','packet=stream_index,pts_time,duration_time,size','-of','json'];
 const probe=(pipe)=>{
  const x=spawnSync('ffprobe',[...options,'-i',pipe?'pipe:0':file],{input:pipe?readFileSync(file):undefined,encoding:'utf8',windowsHide:true,timeout:20000,maxBuffer:4*1024*1024});
  if(x.status!==0)throw Error(x.stderr||x.error?.message);return JSON.parse(x.stdout).packets;
 };
 const local=probe(false),packets=probe(true),firstVideo=packets.findIndex(p=>p.stream_index===1);
 const bytes=packets.reduce((n,p)=>n+Number(p.size),0);
 if(firstVideo<200||packets.length>512||bytes>4*1024*1024)throw Error('Fixture does not fit the tested native queue; review scenario');
 const inc=`static const int actual_order[]={${packets.map(p=>p.stream_index===0?1:0)}};\nstatic const double actual_pts[]={${packets.map(p=>Number(p.pts_time))}};\n`;
 writeFileSync('build/deep_actual_packet_order.inc',inc);
 let actual=host.slice(0,host.lastIndexOf('int main(void)'));
 actual+=`\n#include "deep_actual_packet_order.inc"\nint main(void){
  (void)matrix;(void)trace;(void)SDL_GetError;reset(600);resume_preroll=1;terminal=-541478725;
  stream_count=sizeof(actual_order)/sizeof(*actual_order);
  memcpy(streams,actual_order,sizeof(actual_order));int packet=-1,consumed=0;
  while(running&&consumed<${firstVideo}){
   assert(schedule((unsigned)(queued_bytes*1000.0/bps),1,&packet)==1);
#ifdef CORRECTED
   if(packet==0){
    assert(consumed<188&&running&&!playback_error&&stream_count==(int)(sizeof(actual_order)/sizeof(*actual_order))-1-consumed);
    puts("PASS REAL fMP4 packet order: video before PCM overflow; remaining packets retained.");return 0;
   }
#endif
   assert(packet==1);
   feed(600+actual_pts[consumed],1024);consumed++;
  }
#ifdef CORRECTED
  assert(!"real packet order video was not selected");
#else
  assert(!running&&playback_error==-5&&consumed<${firstVideo}&&stream_count>0);
  printf("REPRODUCED with REAL nonseekable fMP4 packet order: %d decoded blocks hit PCM cap before queued video. Render/decoder/audio device still simulated.\\n",consumed);return 0;
#endif
  return 1;
 }\n`;
 writeFileSync('build/deep_audio_terminal_actual.c',actual);run('actual_order',['build/deep_audio_terminal_actual.c']);
 console.log(JSON.stringify({fixture:'actual FFmpeg fMP4',packets:packets.length,bytes,localFirstVideo:local.findIndex(p=>p.stream_index===1),nonseekableFirstVideo:firstVideo,
  audioSecondsBeforeVideo:packets.slice(0,firstVideo).reduce((n,p)=>n+Number(p.duration_time),0)}));
}
console.log(verify?'POSITIVE REGRESSIONS COMPLETE: host controls/real media order, NOT Switch hardware.':'NEGATIVE AUDIT COMPLETE: exit0 = defects reproduced, NOT a validated release or physical playback.');
