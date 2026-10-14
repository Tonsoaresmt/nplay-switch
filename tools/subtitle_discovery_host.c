#define _POSIX_C_SOURCE 200809L
#include "api.h"
#include "net.h"
typedef struct SDL_Joystick SDL_Joystick;
#include "player.h"
#include "hot_subtitles.h"
#include "subtitle_catalog.h"
#include "subtitle_utf8.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *BASE = "https://fixture.test";
typedef struct { int unused; } ExternalSubtitleStore;
static int load_external_subtitle(const char *url, ExternalSubtitleStore *s, int direct, SDL_atomic_t *cancel) {
    (void)url;(void)s;(void)direct;(void)cancel;return -1;
}
struct SDL_Thread { pthread_t id; int (*fn)(void *); void *arg; };
static int fail_thread, joins, calls, delay_network;
static SDL_atomic_t release_response;
static long response = 200;
static const char *json = "{\"ok\":true,\"duration\":1440,\"subtitles\":["
 "{\"index\":2,\"codec\":\"ass\",\"language\":\"por\",\"title\":\"Portugues\"},"
 "{\"index\":3,\"codec\":\"ass\",\"language\":\"eng\"},"
 "{\"index\":4,\"codec\":\"ass\",\"language\":\"spa\"}]}";
int SDL_AtomicSet(SDL_atomic_t *a,int v){return __atomic_exchange_n(&a->value,v,__ATOMIC_SEQ_CST);}
int SDL_AtomicGet(SDL_atomic_t *a){return __atomic_load_n(&a->value,__ATOMIC_SEQ_CST);}
static void nap(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static void *thread_run(void *p){SDL_Thread *t=p;t->fn(t->arg);return NULL;}
SDL_Thread *SDL_CreateThread(int(*fn)(void*),const char *name,void *arg){
 assert(!strcmp(name,"hot-subtitle"));if(fail_thread)return NULL;
 SDL_Thread *t=calloc(1,sizeof(*t));assert(t);t->fn=fn;t->arg=arg;
 assert(!pthread_create(&t->id,NULL,thread_run,t));return t;
}
void SDL_WaitThread(SDL_Thread *t,int *status){(void)status;assert(!pthread_join(t->id,NULL));joins++;free(t);}
void membuf_free(struct membuf *b){free(b->data);memset(b,0,sizeof(*b));}
long net_get_text_limited(const char *url,size_t cap,long timeout,SDL_atomic_t *cancel,struct membuf *body){
 assert(strstr(url,"/probe")&&cap==256u*1024u&&timeout==5000&&cancel);calls++;
 if(delay_network)while(!SDL_AtomicGet(&release_response)&&!SDL_AtomicGet(cancel))nap();
 // Deliberately returns late success even if cancelled: owner must discard it.
 if(response==200){body->data=strdup(json);assert(body->data);body->len=strlen(json);}
 return response;
}
#include "subtitle_discovery_worker.inc"
typedef struct { int disposition; } AVStream;
typedef struct { AVStream **streams; } AVFormatContext;
#define AV_DISPOSITION_FORCED 64
static const char *stream_norm(AVFormatContext *fmt,int index){(void)fmt;(void)index;return "en";}
static const char *stream_track_title(AVStream *s){(void)s;return NULL;}
static int64_t av_gettime_relative(void){return 1000000;}
static void avformat_close_input(AVFormatContext **p){(void)p;}
static void nplay_curl_avio_close(void *p){(void)p;}
static void player_error_message(const char *s){(void)s;}
static void diag_player_event(const char *a,const char *b,const char *c,...){(void)a;(void)b;(void)c;}
static int hot_subtitle_wait(SDL_Renderer *ren,const char *title,HotSubtitleJob *job){
 (void)ren;(void)title;hot_subtitle_worker(job);return job->result;
}
static int open_catalog(int nsub,const char *sid,HlsManifestTrack *manifest_subtitles,
                        int manifest_subtitle_count,int *origins_out){
 PlayerRequest request={0},*req=&request;
 snprintf(request.playback.hot_session_id,sizeof(request.playback.hot_session_id),"%s",sid);
 SDL_Renderer *ren=NULL;const char *title="fixture";void *avio=NULL;
 AVStream streams[16]={{0}},*ptrs[16];int sidxs[16]={0};
 for(int i=0;i<16;i++){ptrs[i]=&streams[i];sidxs[i]=i;}
 AVFormatContext format={ptrs},*fmt=&format;
 struct {int64_t deadline_us;} open_watch={10000000};
#include "subtitle_discovery_open.inc"
 for(int i=0;i<nsub;i++){origins_out[i]=subtitle_origins[i];assert(SUB_EXTERNAL(i)==(sidxs[i]<0));}
 assert(!SUB_HOT(-1));(void)hot_probe_duration;(void)discovery_retry;
 return nsub;
}
#undef SUB_EXTERNAL
#undef SUB_HOT
int main(void){
 const char *sid="1234567890abcdef_fixture";
 HlsManifestTrack tracks[16]={0};int origins[16]={0};
 assert(open_catalog(1,sid,tracks,0,origins)==4);
 assert(!strcmp(tracks[0].language,"por")&&origins[0]==SUBTITLE_HOT&&origins[3]==SUBTITLE_NATIVE);
 memset(tracks,0,sizeof(tracks));strcpy(tracks[0].uri,"https://fixture.test/pt.m3u8");strcpy(tracks[0].language,"pt-BR");
 assert(open_catalog(1,"",tracks,1,origins)==2&&origins[0]==SUBTITLE_HLS&&origins[1]==SUBTITLE_NATIVE);
 response=503;memset(tracks,0,sizeof(tracks));
 assert(open_catalog(1,sid,tracks,0,origins)==1&&origins[0]==SUBTITLE_NATIVE);
 assert(subtitle_discovery_retryable(503,-1)&&subtitle_discovery_retryable(-28,-1));
 assert(!subtitle_discovery_retryable(404,-1)&&!subtitle_discovery_retryable(410,-1));
 assert(!subtitle_discovery_retryable(-60,-1)&&!subtitle_discovery_retryable(403,-1));
 response=200;delay_network=1;
 for(int i=0;i<100;i++){
  SDL_AtomicSet(&release_response,0);
  int before=joins;SubtitleDiscovery *task=subtitle_discovery_start(sid);assert(task);
  assert(!subtitle_discovery_poll(task));
  if(i%2){subtitle_discovery_stop(task);assert(joins==before+1);continue;}
  SDL_AtomicSet(&release_response,1);
  while(!SDL_AtomicGet(&task->job.done))nap();
  assert(subtitle_discovery_poll(task)&&joins==before+1&&task->job.result==3);
  assert(!subtitle_discovery_poll(task));subtitle_discovery_stop(task);assert(joins==before+1);
 }
 fail_thread=1;assert(!subtitle_discovery_start(sid));fail_thread=0;
 int before=calls;assert(!subtitle_discovery_start("invalid"));assert(calls==before);
 // Appending a retry never reorders or duplicates existing choices, including
 // distinct Portuguese dialogue and signs URLs. Capacity is hard bounded.
 memset(tracks,0,sizeof(tracks));int count=0,streams[16];unsigned char kinds[16];
 HlsManifestTrack t={0};strcpy(t.language,"pt");strcpy(t.uri,"https://fixture.test/sub?track=1");
 assert(subtitle_catalog_add(tracks,streams,kinds,&count,&t,-1,SUBTITLE_HOT)==0);
 assert(subtitle_catalog_add(tracks,streams,kinds,&count,&t,-1,SUBTITLE_HOT)==0&&count==1);
 t.forced=1;
 assert(subtitle_catalog_add(tracks,streams,kinds,&count,&t,-1,SUBTITLE_HOT)==1);
 strcpy(t.uri,"https://fixture.test/sub?track=2");
 assert(subtitle_catalog_add(tracks,streams,kinds,&count,&t,-1,SUBTITLE_HOT)==2);
 for(int i=0;i<20;i++)subtitle_catalog_add(tracks,streams,kinds,&count,&t,i,SUBTITLE_NATIVE);
 assert(count==16&&streams[0]==-1&&kinds[0]==SUBTITLE_HOT);
 puts("PASS: real player discovery + parsers; mixed EN/PT origins, equal counts, bounded retry policy, 100 pthread join/cancel cycles, capacity and stable identities.");
 return 0;
}
