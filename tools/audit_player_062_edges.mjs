// NEGATIVE audit only. exit0 means defects reproduced, never release approval.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
if(!process.argv.includes('--reproduce-062'))throw Error('Use --reproduce-062; negative audit, not release validation');
const base='563245ff1a96a3b412d116b0bc7347b530d2bc48',git='C:/Program Files/Git/cmd/git.exe';
const changed=spawnSync(git,['diff','--name-only',base,'--','source','include','Makefile','tools/av_resume_host.c','tools/player_flow_guards_host.c'],{encoding:'utf8',windowsHide:true});
if(changed.status!==0||changed.stdout.trim())throw Error('Audit requires unchanged .62 runtime/helpers; use its isolated checkout, not mixed dependencies');
const historical=path=>{const r=spawnSync(git,['show',`${base}:${path}`],{encoding:'utf8',windowsHide:true});if(r.status!==0)throw Error(r.stderr);return r.stdout.replace(/\r\n/g,'\n');};
const player=historical('source/player.c'),main=historical('source/main.c'),net=historical('source/net.c');
const section=(s,a,b,from=0)=>{const i=s.indexOf(a,from),j=s.indexOf(b,i);if(i<0||j<i)throw Error(a);return s.slice(i,j);};
mkdirSync('build',{recursive:true});
const save=(n,s)=>writeFileSync(`build/audit062_${n}.inc`,s);
const run=(name,sources)=>{
 const exe=resolve(`build/audit062_${name}.exe`);
 let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Itools/host-stubs','-Iinclude','-Ibuild',...sources,'-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
 if(r.status!==0)throw Error(r.stderr||r.error?.message);
 r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:30000});process.stdout.write(r.stdout||'');
 if(r.status!==0)throw Error(r.stderr||r.error?.message);
};
save('discovery_retry',section(player,'static int subtitle_discovery_retryable(', 'static int progressive_subtitle_block('));
save('discovery_apply',section(player,'        if (subtitle_discovery_poll(discovery)) {','        if (!discovery && discovery_retry'));
save('master',section(player,'typedef struct { char name[96];','static const char *master_audio_norm')+
 section(player,'static void master_audio_info(', '// Escolhe pelo master'));
save('master_map',section(player,'    for (int i = 0; i < count; i++) {','    free(parsed);',player.indexOf('static int player_hls_choose_audio')));
run('tracks',['tools/audit_062_tracks_host.c','source/audio_policy.c','source/subtitle_catalog.c','source/subtitle_choice.c','source/hls_manifest.c']);

save('history',section(main,'static int history_index_for_id(', 'static void apply_downloads('));
save('session',section(main,'static void session_invalid_to_login(', 'static void access_expired_to_login('));
save('profile',section(main,'static void input_profiles(', '// Roteia um botao'));
save('settings',section(main,'static void pump_settings_status(', 'static int load_player_boot_stage('));
save('prefs_gate',section(main,'    if (!g_account_prefs_loaded) {','    if (tab == TAB_SAGAS)'));
let flow=readFileSync('tools/player_flow_guards_host.c','utf8').replace(/\r\n/g,'\n').split('enum {EXIT_REASON_NATURAL')[0];
flow=flow.replace('#include "flow_guards_history.inc"','#include "audit062_history.inc"');
flow=flow.replace('reconciles++;return 0;', 'reconciles++;reconciled_profile=store_profile;return 0;');
writeFileSync('build/audit062_identity_support.inc','static int store_profile=1,reconciled_profile;\n'+flow);
run('identity',['tools/audit_062_identity_host.c','source/cJSON.c']);

save('writer',section(net,'static size_t write_cb(', 'struct file_download_ctx'));
run('budget',['tools/audit_062_budget_host.c','source/cJSON.c']);
run('descriptor',['tools/audit_062_descriptor_host.c','source/api.c','source/playback_restart.c','source/hot_subtitles.c','source/hls_manifest.c','source/cJSON.c']);

if(process.argv.includes('--media-fixtures')){
 const command=(cmd,args,input)=>{const r=spawnSync(cmd,args,{input,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:4*1024*1024});if(r.status!==0)throw Error(r.stderr||r.error?.message);return r;};
 const tail='build/audit062-tail.mp4';
 command('ffmpeg',['-v','error','-y','-f','lavfi','-i','testsrc2=s=320x180:r=24:d=2','-f','lavfi','-i','sine=sample_rate=48000:duration=8',
  '-map','0:v','-map','1:a','-c:v','libx264','-preset','ultrafast','-c:a','aac','-g','24','-movflags','+frag_keyframe+empty_moov+default_base_moof','-frag_duration','1000000',tail]);
 const pk=JSON.parse(command('ffprobe',['-v','error','-show_packets','-show_entries','packet=stream_index,pts_time,duration_time,size','-of','json','pipe:0'],readFileSync(tail)).stdout).packets;
 const lastVideo=pk.findLastIndex(p=>p.stream_index===0),videoEnd=Number(pk[lastVideo].pts_time)+Number(pk[lastVideo].duration_time);
 const audio=pk.slice(lastVideo+1).filter(p=>p.stream_index===1&&Number(p.pts_time)>=videoEnd);
 if(audio.length<220||pk.length>512||pk.reduce((n,p)=>n+Number(p.size),0)>4*1024*1024)throw Error('Review tail fixture bounds');
 save('pcm_helper',section(player,'static int player_queue_pcm(', 'static int create_audio_dec('));
 save('pcm_queue',section(player,'                        double audio_pts = ats != AV_NOPTS_VALUE','\n                    }\n                }\n            }\n        } else if (pkt->stream_index == vidx)'));
 save('schedule',section(player,'        int take = 0;','        int ret = take',player.indexOf('Uint32 audio_before_ms')));
 let host=readFileSync('tools/av_resume_host.c','utf8').replace(/\r\n/g,'\n').split('int main(int argc,char**argv)')[0];
 host=host.replace('static int streams[16],stream_count,terminal;','static int streams[1024],stream_count,terminal;')
  .replaceAll('av_resume_helper.inc','audit062_pcm_helper.inc').replaceAll('av_resume_queue.inc','audit062_pcm_queue.inc').replaceAll('av_resume_schedule.inc','audit062_schedule.inc');
 host+=`\nstatic const double actual_pts[]={${audio.map(p=>Number(p.pts_time))}};\nint main(void){
  (void)matrix;(void)trace;(void)SDL_GetError;reset(${videoEnd});resume_preroll=0;terminal=-541478725;
  stream_count=sizeof(actual_pts)/sizeof(*actual_pts);for(int i=0;i<stream_count;i++)streams[i]=1;
  int packet=-1,consumed=0;while(running&&consumed<(int)(sizeof(actual_pts)/sizeof(*actual_pts))){
   assert(schedule((unsigned)(queued_bytes*1000.0/bps),0,&packet)==1&&packet==1);
   feed(actual_pts[consumed],1024);consumed++;
   // Device plays 1ms while each packet is decoded: nonzero consumption,
   // still much faster than real-time, as a small audio tail can be decoded.
   if(queued_bytes>=192)queued_bytes-=192;
  }
  assert(!running&&playback_error==-5&&stream_count>0&&consumed<(int)(sizeof(actual_pts)/sizeof(*actual_pts)));
  printf("REPRODUCED audio tail after final video: cap at block%d of%zu, queue%.3fs; EOF recovery despite valid remaining audio. CPU/device time modeled, actual AAC packet timestamps.\\n",consumed,sizeof(actual_pts)/sizeof(*actual_pts),queued_bytes/bps);return 0;}
 `;
 writeFileSync('build/audit062_tail.c',host);run('tail',['build/audit062_tail.c']);
 const vfr='build/audit062-vfr.mp4';
 command('ffmpeg',['-v','error','-y','-f','lavfi','-i','testsrc2=s=320x180:r=24:d=9','-f','lavfi','-i','sine=sample_rate=48000:duration=9',
  '-vf',"select='not(mod(n,96))'",'-fps_mode','vfr','-c:v','libx264','-preset','ultrafast','-c:a','aac','-movflags','+faststart',vfr]);
 const frames=JSON.parse(command('ffprobe',['-v','error','-select_streams','v:0','-show_frames','-show_entries','frame=best_effort_timestamp_time','-of','json',vfr]).stdout).frames;
 const pts=frames.map(f=>Number(f.best_effort_timestamp_time));if(pts.length!==3||Math.abs(pts[1]-pts[0]-4)>0.01)throw Error('Review VFR fixture');
 save('wait',section(player,'                    if (delay > 0.001) {','                    if (player_lifecycle_pending()) {',player.indexOf('double delay = vpts - master;')));
 save('vfr_pts',`static const double actual_vfr_pts[]={${pts}};\n`);
 run('vfr',['tools/audit_062_vfr_host.c','source/player_clock.c']);
 console.log(JSON.stringify({realMedia:'FFmpeg8.1.1, not SDK7.1',tailPackets:pk.length,lastVideo:videoEnd,audioTailPackets:audio.length,vfrPTS:pts}));
}
console.log('NEGATIVE .62 AUDIT: exit0 = reproduced defects; no app change/build/release or hardware validation.');
