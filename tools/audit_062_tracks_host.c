#include "audio_policy.h"
#include "subtitle_catalog.h"
#include "subtitle_choice.h"
#include "subtitle_retry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audit062_master.inc"
static void hls_audio(void){
 const char*body="#EXTM3U\n"
 "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",LANGUAGE=\"pt-BR\",NAME=\"audio_0\",DEFAULT=YES,AUTOSELECT=YES,CHARACTERISTICS=\"public.accessibility.describes-video\",URI=\"ad.m3u8\"\n"
 "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",LANGUAGE=\"pt-BR\",NAME=\"audio_1\",DEFAULT=NO,AUTOSELECT=YES,URI=\"pt.m3u8\"\n"
 "#EXT-X-STREAM-INF:BANDWIDTH=900000,AUDIO=\"a\"\nv.m3u8\n";
 HlsManifestTrack parsed_array[16]={0},*parsed=parsed_array;int filterable=0;
 int count=hls_manifest_audio_tracks(body,strlen(body),parsed,16,&filterable);assert(count==2&&filterable);
 MasterAudioTrack tracks[16]={0};
#include "audit062_master_map.inc"
 AudioTrackInfo info[16]={0};int best;master_audio_info(tracks,count,info,&best);
#ifdef CORRECTED
 assert(info[0].is_commentary&&audio_policy_choose(info,count,0,NULL,NULL,0,0,best)==1);
 const char*chars[]={"xpublic.accessibility.describes-video","public.accessibility.describes-video.extra","public.accessibility.describes-music-and-sound,public.accessibility.describes-video"};
 for(int i=0;i<3;i++){
  char m[512];snprintf(m,sizeof(m),"#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,NAME=\"PT\",URI=\"a.m3u8\",CHARACTERISTICS=\"%s\"\n",chars[i]);
  HlsManifestTrack t[1];assert(hls_manifest_audio_tracks(m,strlen(m),t,1,NULL)==1&&t[0].is_commentary==(i==2));
 }
#else
 assert(!info[0].is_commentary&&audio_policy_choose(info,count,0,NULL,NULL,0,0,best)==0);
#endif
 info[0].is_commentary=1;assert(audio_policy_choose(info,count,0,NULL,NULL,0,0,best)==1);
 puts("REPRODUCED HLS accessibility metadata dropped: real parser/master mapping chooses PT audio-description before PT dialogue. Supplying the missing flag changes choice.");
}
#include "audit062_discovery_retry.inc"
typedef struct {struct {int result;long http_code;double duration;} job;HlsManifestTrack tracks[16];} SubtitleDiscovery;
static int subtitle_discovery_poll(SubtitleDiscovery*d){return d!=NULL;}
static void subtitle_discovery_stop(SubtitleDiscovery*d){assert(d);}
static void external_subtitle_keys(SubtitleTrackKey*k,HlsManifestTrack*t,int n){for(int i=0;i<n;i++){snprintf(k[i].language,sizeof(k[i].language),"%s",audio_language_normalize(t[i].language,t[i].name));k[i].forced=t[i].forced;}}
static void external_subtitle_labels(int*h,char n[][128],HlsManifestTrack*t,int count){(void)h;(void)n;(void)t;(void)count;}
static void diag_player_event(const char*a,const char*b,const char*c,...){(void)a;(void)b;(void)c;}
static void forced_then_full(int manual){
 SubtitleDiscovery jobs[2]={0},*discovery=&jobs[0];
 for(int i=0;i<2;i++){jobs[i].job.result=1;jobs[i].job.http_code=200;strcpy(jobs[i].tracks[0].language,"pt");strcpy(jobs[i].tracks[0].uri,i?"https://fixture.invalid/full.vtt":"https://fixture.invalid/signs.vtt");jobs[i].tracks[0].forced=!i;}
 HlsManifestTrack manifest_subtitles[16]={0};int sidxs[16]={0},nsub=0,scur=-1,hud_base=0;
 SubtitleTrackKey subtitle_keys[16]={0};SubtitleChoice g_player_subtitle_choice={0};
 struct {int source_id;} request={1},*req=&request;
 SubtitleRetry subtitle_retry;subtitle_retry_select(&subtitle_retry,-1);
 unsigned char subtitle_origins[16]={0};char hud_sub_names[17][128]={{0}},notice[192]="",subtitle_auto_language[8]="pt";
 unsigned now_ticks=1000,notice_until=0,discovery_due=0,discovery_attempts=1;
 int sequential_stream=0,discovery_retry=1;double dur=0,position=0,*out_dur=&position;
 if(manual)subtitle_auto_language[0]=0;
 for(int event=0;event<2;event++){
  discovery=&jobs[event];
#include "audit062_discovery_apply.inc"
  if(!event){
   if(!manual)assert(subtitle_retry.desired==0&&subtitle_retry.pending&&discovery_retry);
   scur=manual==2?-1:0;subtitle_retry_select(&subtitle_retry,scur);
   subtitle_retry_succeeded(&subtitle_retry,scur);
   if(manual)subtitle_choice_capture(&g_player_subtitle_choice,subtitle_keys,nsub,scur,req->source_id);
  }
 }
 assert(nsub==2&&scur==(manual==2?-1:0)&&!subtitle_keys[1].forced);
 assert(subtitle_choice_preferred(subtitle_keys,nsub,"pt")==1);
#ifdef CORRECTED
 if(!manual)assert(subtitle_auto_language[0]&&g_player_subtitle_choice.automatic&&g_player_subtitle_choice.index==1&&subtitle_retry.desired==1&&subtitle_retry.pending&&!discovery_retry);
 else assert(!g_player_subtitle_choice.automatic&&g_player_subtitle_choice.index==scur&&subtitle_retry.desired==scur&&!subtitle_retry.pending);
 SubtitleChoice before=g_player_subtitle_choice;
 subtitle_choice_capture_auto(&g_player_subtitle_choice,subtitle_keys,nsub,nsub,1);
 assert(!memcmp(&before,&g_player_subtitle_choice,sizeof(before)));
#else
 assert(!subtitle_auto_language[0]&&g_player_subtitle_choice.valid&&subtitle_retry.desired==0&&!discovery_retry);
#endif
 assert(notice_until&&discovery_due);
 puts("REPRODUCED asynchronous forced-first trap: auto applies PT signs, full PT arrives on next probe but stays unselected; later full-PT preference is never reapplied.");
}
int main(void){hls_audio();forced_then_full(0);
#ifdef CORRECTED
 forced_then_full(1);forced_then_full(2);
#endif
 return 0;}
