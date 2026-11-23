// Real extracted selection/application C; demux metadata and render APIs fake.
#include "audio_policy.h"
#include "subtitle_catalog.h"
#include "hot_subtitles.h"
#include "subtitle_choice.h"
#include "subtitle_retry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef CORRECTED
#define REPORT(bad,good) puts(good)
#else
#define REPORT(bad,good) puts(bad)
#endif
typedef struct {const char *value;} AVDictionaryEntry;
typedef struct {int disposition;AVDictionaryEntry*metadata;} AVStream;
typedef struct {AVStream**streams;} AVFormatContext;
#define AV_DISPOSITION_DEFAULT 1
#define AV_DISPOSITION_COMMENT 8
#define AV_DISPOSITION_VISUAL_IMPAIRED 256
#define AV_DISPOSITION_ORIGINAL 4
static AVDictionaryEntry*av_dict_get(AVDictionaryEntry*m,const char*k,void*p,int f){(void)p;(void)f;assert(!strcmp(k,"language"));return m;}
static const char*stream_track_title(AVStream*s){(void)s;return NULL;}
static const char*stream_norm(AVFormatContext*f,int i){return f->streams[i]->metadata->value;}

static void commentary(void){
 AVDictionaryEntry langs[2]={{"pt"},{"pt"}};
 AVStream streams[2]={{AV_DISPOSITION_COMMENT,&langs[0]},{0,&langs[1]}};
 AVStream*ptrs[2]={&streams[0],&streams[1]};AVFormatContext format={ptrs},*fmt=&format;
 AudioTrackInfo audio_tracks[16]={0};int aidxs[2]={0,1};int naud_ui=2,audio_from_master=0,known_audio=0,pt_audio=-1;
 char audio_map[192]="";
#include "deep_audio_metadata.inc"
 int chosen=audio_policy_choose(audio_tracks,2,0,NULL,NULL,0,0,1);
 assert(known_audio==2&&pt_audio==0);
#ifdef CORRECTED
 assert(chosen==1&&audio_tracks[0].is_commentary);
#else
 assert(chosen==0&&!audio_tracks[0].is_commentary);
#endif
 audio_tracks[0].is_commentary=1;assert(audio_policy_choose(audio_tracks,2,0,NULL,NULL,0,0,1)==1);
 REPORT("REPRODUCED native PT commentary metadata loss.","PASS native PT commentary disposition retained; normal PT selected.");
 assert(audio_policy_choose(audio_tracks,2,0,NULL,"pt",2,1,1)==
#ifdef CORRECTED
 1
#else
 0
#endif
 );
 REPORT("REPRODUCED PT continuity selects commentary.","PASS PT language remap excludes commentary; exact manual priority remains independently tested.");
 // Film says English original/default; Japanese is a dubbed alternative.
 // Native mapping retains only default, while policy hard-prefers Japanese.
 langs[0].value="eng";langs[1].value="jpn";streams[0].disposition=1|4;streams[1].disposition=2;
 memset(audio_tracks,0,sizeof(audio_tracks));known_audio=0;pt_audio=-1;audio_map[0]=0;
#include "deep_audio_metadata.inc"
 assert(audio_policy_choose(audio_tracks,2,1,NULL,NULL,0,0,0)==
#ifdef CORRECTED
 0
#else
 1
#endif
 );
 REPORT("REPRODUCED original film selects Japanese over explicit original.","PASS native ORIGINAL disposition wins over Japanese dub.");
}
static int automatic(int forced_first){
 HlsManifestTrack manifest_subtitles[16]={0};
 const char*sign="#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"s\",LANGUAGE=\"pt-BR\",NAME=\"Signs\",FORCED=YES,DEFAULT=YES,URI=\"signs.m3u8\"\n";
 const char*full="#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"s\",LANGUAGE=\"por\",NAME=\"Dialogue\",FORCED=NO,DEFAULT=NO,URI=\"pt.m3u8\"\n";
 char master[1024];snprintf(master,sizeof(master),"#EXTM3U\n%s%s",forced_first?sign:full,forced_first?full:sign);
 assert(hls_manifest_subtitle_tracks(master,strlen(master),manifest_subtitles,16)==2);
 int sub_chosen=0,naud=1,nsub=2,scur=-1,pt_audio=-1,known_audio=1,acur=0,aidxs[1]={0},sidxs[2]={0};
 SubtitleTrackKey subtitle_keys[2]={0};
 for(int i=0;i<2;i++){strcpy(subtitle_keys[i].language,"pt");subtitle_keys[i].forced=manifest_subtitles[i].forced;}
 (void)subtitle_keys;(void)sidxs;
 struct {int audio_pref;} request={1},*req=&request;
 AVDictionaryEntry lang={"ja"};AVStream stream={0,&lang},*streams[1]={&stream};AVFormatContext format={streams},*fmt=&format;
#define SUB_EXTERNAL(index) ((index)>=0)
#include "deep_sub_auto.inc"
#undef SUB_EXTERNAL
 assert(scur>=0&&scur<2);return manifest_subtitles[scur].forced;
}
#include "deep_discovery_retry.inc"
typedef struct {struct {int result;long http_code;double duration;} job;HlsManifestTrack tracks[16];} SubtitleDiscovery;
static int subtitle_discovery_poll(SubtitleDiscovery*d){return d!=NULL;}
static int stops;
static void subtitle_discovery_stop(SubtitleDiscovery*d){assert(d);stops++;}
static void external_subtitle_keys(SubtitleTrackKey*k,HlsManifestTrack*t,int n){for(int i=0;i<n;i++){strcpy(k[i].language,t[i].language);k[i].forced=t[i].forced;}}
static void external_subtitle_labels(int*h,char n[][128],HlsManifestTrack*t,int count){(void)h;(void)n;(void)t;assert(count==1);}
static void diag_player_event(const char*a,const char*b,const char*c,...){(void)a;(void)b;(void)c;}
static void late_discovery(int foreign_only,int manual_off){
 SubtitleDiscovery task={.job={1,200,1440}},*discovery=&task;
 strcpy(task.tracks[0].language,foreign_only?"en":"pt");strcpy(task.tracks[0].uri,"https://fixture.invalid/pt.vtt");
 HlsManifestTrack manifest_subtitles[16]={0};int sidxs[16]={0},nsub=0,scur=-1,hud_base=0;
 SubtitleTrackKey subtitle_keys[16]={0};
 struct {int source_id;} request={1},*req=&request;
 SubtitleChoice g_player_subtitle_choice={0};
 SubtitleRetry subtitle_retry;subtitle_retry_select(&subtitle_retry,-1);
 char subtitle_auto_language[8]="pt";
 if(manual_off)subtitle_auto_language[0]=0;
 (void)req;(void)g_player_subtitle_choice;(void)subtitle_auto_language;(void)subtitle_retry;
 unsigned char subtitle_origins[16]={0};char hud_sub_names[17][128]={{0}},notice[192]="";
 unsigned now_ticks=1000,notice_until=0,discovery_due=0,discovery_attempts=1;int sequential_stream=1,discovery_retry=1;
 double dur=0,position=0,*out_dur=&position;
#include "deep_discovery_apply.inc"
 assert(nsub==1&&scur==-1&&!discovery&&dur==1440&&notice_until>now_ticks&&discovery_due>now_ticks);
#ifdef CORRECTED
 assert(discovery_retry==foreign_only);
 if(!foreign_only&&!manual_off){
  assert(subtitle_retry.desired==0&&subtitle_retry.pending&&subtitle_retry.deadline==now_ticks);
  assert(g_player_subtitle_choice.valid&&g_player_subtitle_choice.index==0);
 }else assert(subtitle_retry.desired==-1&&!subtitle_retry.pending&&!g_player_subtitle_choice.valid);
#else
 assert(!discovery_retry);
#endif
 REPORT("REPRODUCED late PT never auto-selected.","PASS late track selection honors PT presence and manual OFF.");
 assert(!subtitle_discovery_retryable(200,1)&&subtitle_discovery_retryable(200,0));
 REPORT("REPRODUCED foreign-only success ends probing.","PASS discovery call uses PT completeness rather than any nonempty result.");
}
static void capacity(void){
 cJSON*probe=cJSON_CreateObject();cJSON_AddBoolToObject(probe,"ok",1);cJSON*subs=cJSON_AddArrayToObject(probe,"subtitles");
 for(int i=0;i<17;i++){cJSON*t=cJSON_CreateObject();cJSON_AddNumberToObject(t,"index",i);cJSON_AddStringToObject(t,"codec","ass");cJSON_AddStringToObject(t,"language",i==16?"por":"eng");cJSON_AddItemToArray(subs,t);}
 HlsManifestTrack tracks[16]={0};int n=hot_subtitle_tracks(probe,"https://fixture.invalid","1234567890abcdef_fixture",tracks,16);
 assert(n==16);
 int pt=0;for(int i=0;i<n;i++)if(!strcmp(tracks[i].language,"por"))pt++;
#ifdef CORRECTED
 assert(pt==1);
#else
 assert(pt==0);
#endif
 cJSON_Delete(probe);
 REPORT("REPRODUCED PT17 omitted by capacity16.","PASS PT17 retained within unchanged capacity16.");
}
int main(void){commentary();
#ifdef CORRECTED
 assert(!automatic(1)&&!automatic(0));
#else
 assert(automatic(1)&&!automatic(0));
#endif
 puts("Checked forced/full PT selection in both manifest orders.");late_discovery(0,0);late_discovery(1,0);late_discovery(0,1);capacity();
#ifdef CORRECTED
 puts("PASS: all seven audio/subtitle reproductions now assert corrected behavior.");
#endif
 return 0;}
