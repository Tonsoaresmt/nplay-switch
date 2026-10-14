#include "api.h"
typedef struct SDL_Joystick SDL_Joystick;
#include "player.h"
#include "hot_subtitles.h"
#include "hls_manifest.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

typedef struct {const char *sid;HlsManifestTrack *tracks;double duration;} HotSubtitleJob;
typedef struct {int count,external,probes;double duration;} Outcome;
static const char *probe_json;
static int probe_status,probe_calls,close_calls;
static int64_t av_gettime_relative(void){return 1000000;}
static void avformat_close_input(void **p){(void)p;close_calls++;}
static void nplay_curl_avio_close(void *p){(void)p;close_calls++;}
static void player_error_message(const char *s){assert(!strcmp(s,"Abertura cancelada"));}
static void diag_player_event(const char*a,const char*b,const char*c,...){(void)a;(void)b;(void)c;}
static int hot_subtitle_wait(SDL_Renderer *r,const char *title,HotSubtitleJob *job){
 (void)r;(void)title;probe_calls++;
 if(probe_status!=200)return -1;
 cJSON *j=cJSON_Parse(probe_json);assert(j);
 int count=hot_subtitle_tracks(j,"https://fixture.test",job->sid,job->tracks,HLS_MANIFEST_TRACK_CAP);
 job->duration=1440;cJSON_Delete(j);return count;
}
static int discovery(int nsub,const char *sid,HlsManifestTrack *manifest_subtitles,
                     int manifest_subtitle_count,Outcome *out){
 PlayerRequest request={0},*req=&request;
 snprintf(request.playback.hot_session_id,sizeof(request.playback.hot_session_id),"%s",sid);
 SDL_Renderer *ren=NULL;const char *title="Fixture";void *fmt=NULL,*avio=NULL;
 struct {int64_t deadline_us;} open_watch={10000000};
 probe_calls=close_calls=0;
#include "audit_subtitle_discovery.inc"
 out->count=nsub;out->external=manifest_subtitle_fallback;
 out->probes=probe_calls;out->duration=hot_probe_duration;
 (void)hot_external_subtitles;return 0;
}
int main(void){
 const char *sid="1234567890abcdef_fixture";
 probe_json="{\"ok\":true,\"subtitles\":["
  "{\"index\":2,\"codec\":\"ass\",\"language\":\"por\",\"title\":\"Portugues Brasil\"},"
  "{\"index\":3,\"codec\":\"subrip\",\"language\":\"eng\",\"title\":\"English\"},"
  "{\"index\":4,\"codec\":\"ass\",\"language\":\"spa\",\"title\":\"Espanol\"}]}";
 probe_status=200;HlsManifestTrack tracks[16]={0};Outcome o={0};
 assert(!discovery(0,sid,tracks,0,&o)&&o.count==3&&o.external&&o.probes==1);
 assert(!strcmp(tracks[0].language,"por"));
 puts("CONTROL: no embedded subtitle -> external PT/EN/ES are discovered (3 tracks).");
 memset(tracks,0,sizeof(tracks));
 assert(!discovery(1,sid,tracks,0,&o)&&o.count==1&&!o.external&&!o.probes);
 puts("REPRODUCED: one embedded EN hides external PT/EN/ES; probe never runs, menu stays at 1 track.");
 const char *master="#EXTM3U\n#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\"Portugues\",LANGUAGE=\"pt-BR\",URI=\"pt.m3u8\"\n";
 int n=hls_manifest_subtitle_tracks(master,strlen(master),tracks,16);
 assert(n==1&&!strcmp(tracks[0].language,"pt-BR"));
 assert(!discovery(1,"",tracks,n,&o)&&o.count==1&&!o.external);
 puts("REPRODUCED: native EN count=1 + master PT count=1 ignores the PT rendition (counts compared, not identity).");
 char full[2048];snprintf(full,sizeof(full),"%s#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\"English\",LANGUAGE=\"en\",URI=\"en.m3u8\"\n#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\"Espanol\",LANGUAGE=\"es\",URI=\"es.m3u8\"\n",master);
 n=hls_manifest_subtitle_tracks(full,strlen(full),tracks,16);assert(n==3);
 assert(!discovery(1,"",tracks,n,&o)&&o.count==3&&o.external&&!o.probes);
 puts("CONTROL: master PT/EN/ES count=3 > native=1 exposes all 3; not every R2 playback is affected.");
 probe_status=503;memset(tracks,0,sizeof(tracks));
 assert(!discovery(0,sid,tracks,0,&o)&&!o.count&&o.probes==1);
 puts("REPRODUCED: metadata pending (503) -> one initial probe and zero tracks; no automatic discovery retry in this block.");
 probe_status=200;
 assert(!discovery(0,sid,tracks,0,&o)&&o.count==3);
 puts("CONTROL: same simulated source later ready -> explicit new probe discovers PT/EN/ES.");
 assert(!discovery(0,"invalid",tracks,0,&o)&&!o.count&&!o.probes);
 puts("CONTROL: invalid SID fails closed, no request or borrowed source substitution.");
 assert(!close_calls);
 return 0;
}
