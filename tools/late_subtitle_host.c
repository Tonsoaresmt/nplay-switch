#include "subtitle_retry.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HLS_MANIFEST_URI_MAX 2048
typedef uint32_t Uint32;
typedef struct {int value;} SDL_atomic_t;
typedef struct {SDL_atomic_t done,ready,cancel;int result,parser;char url[HLS_MANIFEST_URI_MAX];void*mutex,*thread;} ProgressiveSubtitle;
static ProgressiveSubtitle task;
static int SDL_AtomicGet(SDL_atomic_t*a){return a->value;}
static void SDL_AtomicSet(SDL_atomic_t*a,int value){a->value=value;}
static int stops,clears,decoders;
static void progressive_subtitle_stop(ProgressiveSubtitle*p){if(!p)return;if(p==&task)assert(p->done.value);else free(p);stops++;}
static int fail_alloc,fail_mutex,fail_thread,token;
static void*test_calloc(size_t n,size_t size){return fail_alloc?NULL:calloc(n,size);}
static void*SDL_CreateMutex(void){return fail_mutex?NULL:&token;}
static int progressive_subtitle_block(void*p,const char*b,size_t n){(void)p;(void)b;(void)n;return 1;}
static int progressive_subtitle_worker(void*p){(void)p;return 0;}
static void vtt_stream_init(int*p,int(*fn)(void*,const char*,size_t),void*ctx){assert(p&&fn==progressive_subtitle_block&&ctx);}
static void*SDL_CreateThread(int(*fn)(void*),const char*name,void*ctx){assert(fn==progressive_subtitle_worker&&!strcmp(name,"subtitle-stream")&&ctx);return fail_thread?NULL:&token;}
#define calloc test_calloc
#include "late_subtitle_factory.inc"
#undef calloc
typedef struct {ProgressiveSubtitle*progressive;double time_offset;} ExternalSubtitleStore;
static void external_subtitle_clear(ExternalSubtitleStore*s){clears++;memset(s,0,sizeof(*s));}
static void avcodec_free_context(void**p){*p=NULL;decoders++;}
static void subtitle_queue_reset(int*p){*p=0;}
static ProgressiveSubtitle *pending_hot_subtitle;
static int pending_hot_choice,scur,g_player_subtitle_index,subtitles;
static Uint32 pending_hot_started,now_ticks,notice_until;
static double restart_base;
static void*sctx;
static ExternalSubtitleStore external_subtitles;
static SubtitleRetry subtitle_retry;
static char notice[96];
static Uint32 SDL_GetTicks(void){return now_ticks;}
static void diag_player_event(const char*a,const char*b,const char*c,...){(void)a;(void)b;(void)c;}
static void initial(int sub_result){
#include "late_subtitle_initial.inc"
}
static void poll(void){
#include "late_subtitle_poll.inc"
}
static void reset(void){
 memset(&task,0,sizeof(task));pending_hot_subtitle=&task;
 pending_hot_choice=2;scur=0;g_player_subtitle_index=1;subtitles=7;
 pending_hot_started=UINT32_MAX-100;now_ticks=pending_hot_started+50;
 restart_base=600;external_subtitles=(ExternalSubtitleStore){NULL,42};
 subtitle_retry_select(&subtitle_retry,2);stops=clears=decoders=0;sctx=&task;
}
int main(void){
 reset();subtitle_retry_select(&subtitle_retry,0);initial(-2);assert(scur==-1&&!subtitle_retry.pending); // cancel is never retried
 reset();subtitle_retry_select(&subtitle_retry,0);initial(-1);assert(scur==-1&&subtitle_retry.pending&&subtitle_retry.attempts==1);
 reset();initial(0);assert(scur==0&&!subtitle_retry.pending);
 assert(!start_hot_subtitle(NULL,0));fail_alloc=1;assert(!start_hot_subtitle("https://fixture.invalid/a",0));fail_alloc=0;
 fail_mutex=1;assert(!start_hot_subtitle("https://fixture.invalid/a",0));fail_mutex=0;
 fail_thread=1;assert(!start_hot_subtitle("https://fixture.invalid/a",0));fail_thread=0;
 char large[HLS_MANIFEST_URI_MAX+2];memset(large,'a',sizeof(large));large[sizeof(large)-1]=0;
 assert(!start_hot_subtitle(large,0));large[HLS_MANIFEST_URI_MAX-2]=0;assert(!start_hot_subtitle(large,600));
 ProgressiveSubtitle*created=start_hot_subtitle("https://fixture.invalid/a?track=1",600);
 assert(created&&strstr(created->url,"&from=600.000"));progressive_subtitle_stop(created);
 for(int i=0;i<1000;i++){
  reset();poll();assert(pending_hot_subtitle==&task&&!stops&&!clears&&scur==0);
  task.ready.value=1;poll();assert(!pending_hot_subtitle&&!stops&&clears==1&&decoders==1);
  assert(external_subtitles.progressive==&task&&external_subtitles.time_offset==600&&scur==2&&g_player_subtitle_index==3);
  poll();assert(clears==1); // applied once, live session is now owned by store
  reset();subtitle_retry_select(&subtitle_retry,-1);task.ready.value=1;poll();
  assert(task.cancel.value&&pending_hot_subtitle&&!stops&&!clears&&scur==0);
  task.done.value=1;poll();assert(!pending_hot_subtitle&&stops==1&&!clears&&!subtitle_retry.pending);
  reset();subtitle_retry_select(&subtitle_retry,3);task.ready.value=1;poll();
  assert(task.cancel.value&&!clears);task.done.value=1;poll();assert(stops==1&&!subtitle_retry.pending);
  reset();task.done.value=1;task.ready.value=1;task.result=-1;poll();
  assert(stops==1&&!clears&&subtitle_retry.pending&&subtitle_retry.attempts==1&&scur==0);
  reset();now_ticks=pending_hot_started+15000;poll();
  assert(task.cancel.value&&pending_hot_subtitle&&!stops);task.done.value=1;poll();
  assert(stops==1&&subtitle_retry.pending&&subtitle_retry.attempts==1&&!clears);
  reset();task.done.value=1;poll();assert(stops==1&&subtitle_retry.pending&&!clears);
 }
 puts("PASS actual late-subtitle publication: 6000 scenarios, delayed ready, OFF/new choice, late failed result, timeout/clock wrap, preserve old store and no join before worker done. Worker/network/device simulated.");
 return 0;
}
