#define _POSIX_C_SOURCE 200809L
#include "api.h"
#include "net.h"
#include "diag.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef int32_t Sint32;
// The API harness deliberately uses tools/host-stubs/curl/curl.h, not curl.
typedef int CURLcode;
typedef enum {CURLINFO_NAMELOOKUP_TIME,CURLINFO_CONNECT_TIME,
              CURLINFO_APPCONNECT_TIME,CURLINFO_STARTTRANSFER_TIME} CURLINFO;
#define CURLE_OK 0
#define WIN_W 1280
#define WIN_H 720
enum {JOY_A=0,JOY_B=1,JOY_MINUS=10};
enum {SC_LOGIN=0,SC_SERIES=3};
static const SDL_Color C_BG={0},C_TEXT={0},C_MUT={0},C_ACC={0},C_CARD={0};
static SDL_Renderer *gRen;
static int g_running=1,g_screen=SC_SERIES,g_playback_ui_return_item;
static const char *g_toast="Operation timed out";
const char *BASE="https://nplay.test";
char g_token[640]="fixture-token";
static pthread_t renderer;
static int active,created,joined,draws,choice,chosen,source_calls,fail_source,fail_progress;
static int cancel_source,fail_thread,expired,allocations,release_events;
static int input_type;
static const char *progress_fixture="{\"progress\":null}";
struct SDL_Thread {pthread_t id;int(*fn)(void*);void*arg;int rc;};
static void nap(unsigned ms){struct timespec t={ms/1000,(long)(ms%1000)*1000000};nanosleep(&t,NULL);}
Uint32 SDL_GetTicks(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (Uint32)(t.tv_sec*1000u+t.tv_nsec/1000000u);}
int SDL_AtomicGet(SDL_atomic_t*v){return __atomic_load_n(&v->value,__ATOMIC_SEQ_CST);}
int SDL_AtomicSet(SDL_atomic_t*v,int n){return __atomic_exchange_n(&v->value,n,__ATOMIC_SEQ_CST);}
static void *worker(void *p){SDL_Thread*t=p;t->rc=t->fn(t->arg);__atomic_sub_fetch(&active,1,__ATOMIC_SEQ_CST);return NULL;}
SDL_Thread *SDL_CreateThread(int(*fn)(void*),const char*name,void*arg){
 if((fail_thread==1 && !strcmp(name,"resolve-play")) ||
    (fail_thread==2 && !strcmp(name,"resolve-progress")))return NULL;
 SDL_Thread*t=calloc(1,sizeof(*t));assert(t);t->fn=fn;t->arg=arg;
 __atomic_add_fetch(&active,1,__ATOMIC_SEQ_CST);__atomic_add_fetch(&created,1,__ATOMIC_SEQ_CST);
 assert(!pthread_create(&t->id,NULL,worker,t));return t;
}
void SDL_WaitThread(SDL_Thread*t,int*out){assert(!pthread_join(t->id,NULL));if(out)*out=t->rc;free(t);__atomic_add_fetch(&joined,1,__ATOMIC_SEQ_CST);}
void SDL_Delay(Uint32 ms){(void)ms;nap(1);}
static int in_retry;
int SDL_PollEvent(SDL_Event*e){
 if(!chosen && (in_retry || cancel_source)){
  memset(e,0,sizeof(*e));chosen=1;e->type=SDL_JOYBUTTONDOWN;e->jbutton.button=cancel_source?JOY_B:choice;
  if(input_type>=1 && input_type<=3){e->type=SDL_FINGERDOWN;e->tfinger.x=(input_type==1?300.f:input_type==2?700.f:1100.f)/1280.f;e->tfinger.y=(input_type==3?50.f:450.f)/720.f;}
  if(input_type==4)e->type=SDL_QUIT;
  return 1;
 }return 0;
}
int SDL_SetRenderDrawColor(SDL_Renderer*r,unsigned char a,unsigned char b,unsigned char c,unsigned char d){(void)r;(void)a;(void)b;(void)c;(void)d;assert(pthread_equal(pthread_self(),renderer));return 0;}
int SDL_RenderClear(SDL_Renderer*r){(void)r;return 0;}
void SDL_RenderPresent(SDL_Renderer*r){(void)r;draws++;}
static void appletSetMediaPlaybackState(int n){(void)n;}
static void toast(const char*s){assert(s);}
static void access_expired_to_login(void){g_screen=SC_LOGIN;}
static int ui_header_action_hit(int x,int y){return x>1000 && y<100;}
static void ui_header(const char*a,const char*b,const char*c){(void)a;(void)b;(void)c;}
static void ui_popcorn_draw(SDL_Renderer*r,int a,int b,int c){(void)r;(void)a;(void)b;(void)c;}
static void text_center_at(const char*s,int a,int b,int c,SDL_Color d,int e){(void)s;(void)a;(void)b;(void)c;(void)d;(void)e;}
static void text_center(const char*s,int a,SDL_Color b,int c){(void)s;(void)a;(void)b;(void)c;}
static void text_clip(const char*s,int a,int b,SDL_Color c,int d,int e){(void)s;(void)a;(void)b;(void)c;(void)d;(void)e;}
static void fill_rect(int a,int b,int c,int d,SDL_Color e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void diag_set_boot_stage(const char*s){assert(pthread_equal(pthread_self(),renderer));in_retry=!strcmp(s,"00 aguardando escolha apos falha de fonte");}
void diag_player_event(const char*a,const char*b,const char*fmt,...){(void)a;(void)fmt;if(!strcmp(b,"joined"))release_events++;}
void diag_network_event(const char*m,const char*p,long code,unsigned ms,size_t bytes){(void)ms;(void)bytes;(void)code;assert(!strcmp(m,"GET")||!strcmp(m,"POST"));assert(!strchr(p,'?'));}
static void *json_alloc(size_t n){void*p=malloc(n);if(p)__atomic_add_fetch(&allocations,1,__ATOMIC_SEQ_CST);return p;}
static void json_free(void*p){if(p){__atomic_sub_fetch(&allocations,1,__ATOMIC_SEQ_CST);free(p);}}
void membuf_free(struct membuf*m){free(m->data);memset(m,0,sizeof(*m));}
#include "playback_resolution_write.inc"
#undef curl_easy_getinfo
static _Thread_local double timing_seconds;
static CURLcode timing_getinfo(CURL*c,CURLINFO i,double*out){(void)c;(void)i;*out=timing_seconds;return CURLE_OK;}
#define curl_easy_getinfo timing_getinfo
#include "playback_request_timing.inc"
#undef curl_easy_getinfo
static void *timing_worker(void *arg){
 timing_seconds=(double)(uintptr_t)arg;
 for(int i=0;i<10000;i++){
  capture_request_timing(NULL);NetRequestTiming t={0};net_take_request_timing(&t);
  assert(t.dns_ms==(unsigned)timing_seconds*1000 && t.connect_ms==t.dns_ms && t.tls_ms==t.dns_ms && t.first_byte_ms==t.dns_ms);
  net_take_request_timing(&t);assert(!t.dns_ms && !t.first_byte_ms);
 }return NULL;
}
long net_request_timeout_cancel(const char*u,const char*m,const char*b,const char*t,
 struct membuf*out,const char**err,long connect,long total,SDL_atomic_t*cancel){
 assert(!pthread_equal(pthread_self(),renderer));assert(!strcmp(t,"fixture-token"));(void)b;
 int is_progress=!strcmp(m,"GET");
 assert(!strcmp(u,is_progress?"https://nplay.test/api/sync/progress/42":"https://nplay.test/api/stream/42"));
 assert(connect==(is_progress?2:8) && total==(is_progress?5:20));
 if(!is_progress){assert(out->limit==64u*1024u);__atomic_add_fetch(&source_calls,1,__ATOMIC_SEQ_CST);}
 nap(cancel_source?15:2);*err=NULL;
 if(cancel && SDL_AtomicGet(cancel))return -42;
 int count=__atomic_load_n(&source_calls,__ATOMIC_SEQ_CST);
 if((is_progress && fail_progress)||(!is_progress && count<=fail_source)){*err="Operation timed out";return -28;}
 if(!is_progress && expired){const char*s="{\"reason\":\"expired\"}";assert(write_cb((char*)s,1,strlen(s),out)==strlen(s));return 401;}
 const char*s=is_progress?progress_fixture:"{\"session_id\":9,\"source_id\":7,\"container\":\"mp4\",\"play_url\":\"/api/play/42\"}";
 assert(write_cb((char*)s,1,strlen(s),out)==strlen(s));return 200;
}
long net_request_timeout(const char*u,const char*m,const char*b,const char*t,struct membuf*o,const char**e,long c,long a){return net_request_timeout_cancel(u,m,b,t,o,e,c,a,NULL);}
#include "playback_resolution.inc"
static int guard_cancel,stop_calls,guard_queries;
static cJSON*ui_request_get(SDL_Renderer*r,const char*p,int*running,int*cancelled){
 (void)r;(void)running;assert(!strcmp(p,"/api/sync/progress/42"));guard_queries++;*cancelled=guard_cancel;
 return progress_fixture?cJSON_Parse(progress_fixture):NULL;
}
static long ui_send(const char*p,const char*m,const char*b){assert(!strcmp(p,"/api/stream/42/stop") && !strcmp(m,"POST") && !strcmp(b,"{}"));stop_calls++;return 200;}
static int progress_guard(cJSON**prefetched_progress,int progress_fetched,double*position,int*done){
 int itemId=42;PlaybackSource src={.session_id=9};
#include "playback_progress_guard.inc"
 *position=start;*done=completed;return 1;
}
static void reset(void){assert(!active && created==joined && !allocations);g_running=1;g_screen=SC_SERIES;chosen=in_retry=0;source_calls=fail_source=fail_progress=cancel_source=fail_thread=expired=release_events=input_type=0;draws=0;choice=JOY_B;progress_fixture="{\"progress\":null}";}
int main(void){
 renderer=pthread_self();cJSON_Hooks hooks={json_alloc,json_free};cJSON_InitHooks(&hooks);
 reset();fail_progress=1;ResolvePoll p={.item_id=42};
 SDL_Thread*pt=SDL_CreateThread(resolve_progress_thread,"resolve-progress",&p);SDL_WaitThread(pt,NULL);
 assert(!p.progress_fetched && !p.progress); // Published .53 fails this assertion.
 puts("PASS: timed-out progress is UNKNOWN, not position zero.");
 const char*bad[]={"{}","[]","{\"progress\":[]}","{\"progress\":{}}","{\"progress\":{\"position_seconds\":-1}}","{\"progress\":{\"position_seconds\":1e999}}","{\"progress\":{\"position_seconds\":20,\"completed\":2}}"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){cJSON*j=cJSON_Parse(bad[i]);assert(!playback_progress_valid(j));cJSON_Delete(j);}
 const char*good[]={"{\"progress\":null}","{\"progress\":{\"position_seconds\":600,\"completed\":0}}","{\"progress\":{\"position_seconds\":600,\"completed\":true}}"};
 for(unsigned i=0;i<sizeof(good)/sizeof(*good);i++){cJSON*j=cJSON_Parse(good[i]);assert(playback_progress_valid(j));cJSON_Delete(j);}
 PlaybackSource out={0};cJSON*progress=NULL;int fetched=0;
 reset();assert(!resolve_open_with_animation(42,"Anime",&out,&progress,&fetched));assert(out.session_id==9 && fetched && progress && source_calls==1 && release_events==1);cJSON_Delete(progress);
 reset();fail_source=1;assert(resolve_open_with_animation(42,"Anime",&out,&progress,&fetched)==-2);assert(source_calls==1 && !active && !progress);
 puts("PASS: source timeout returns a cancellable choice, no automatic POST retry.");
 reset();fail_source=1;choice=JOY_A;assert(!resolve_open_with_animation(42,"Anime",&out,&progress,&fetched));assert(source_calls==2 && release_events==2 && fetched);cJSON_Delete(progress);
 puts("PASS: explicit retry joins both old workers before reusing their state.");
 reset();expired=1;assert(resolve_open_with_animation(42,"Anime",&out,&progress,&fetched)==-1);assert(g_screen==SC_LOGIN && source_calls==1);
 reset();cancel_source=1;assert(resolve_open_with_animation(42,"Anime",&out,&progress,&fetched)==-2);assert(!active && !progress);
 reset();fail_thread=1;assert(resolve_open_with_animation(42,"Anime",&out,&progress,&fetched)==-2);assert(!source_calls && !active);
 reset();fail_thread=2;assert(!resolve_open_with_animation(42,"Anime",&out,&progress,&fetched));assert(source_calls==1 && !progress && !fetched);
 reset();fail_source=1;input_type=1;assert(!resolve_open_with_animation(42,"Anime",&out,&progress,&fetched));assert(source_calls==2);cJSON_Delete(progress);
 for(int i=2;i<=4;i++){reset();fail_source=1;input_type=i;assert(resolve_open_with_animation(42,"Anime",&out,&progress,&fetched)==-2);assert(source_calls==1 && !active);}
 reset();cancel_source=1;input_type=3;assert(resolve_open_with_animation(42,"Anime",&out,&progress,&fetched)==-2);assert(!active);
 reset();cancel_source=1;input_type=4;assert(resolve_open_with_animation(42,"Anime",&out,&progress,&fetched)==-2);assert(!g_running && !active);
 puts("PASS: touch retry/back/header, quit while waiting, missing progress thread; owned workers always joined.");
 reset();double position=-1;int done=-1;guard_cancel=stop_calls=guard_queries=0;progress=NULL;progress_fixture=NULL;
 assert(!progress_guard(&progress,0,&position,&done));assert(position==-1 && done==-1 && stop_calls==1 && guard_queries==1);
 progress_fixture="[]";assert(!progress_guard(&progress,0,&position,&done));assert(position==-1);
 progress_fixture="{\"progress\":{\"position_seconds\":1200,\"completed\":0}}";
 assert(progress_guard(&progress,0,&position,&done));assert(position==1200 && !done);
 progress_fixture="{\"progress\":null}";assert(progress_guard(&progress,0,&position,&done));assert(position==0);
 progress=cJSON_Parse("{\"progress\":{\"position_seconds\":600,\"completed\":true}}");int queries=guard_queries;
 assert(progress_guard(&progress,1,&position,&done));assert(position==600 && done==1 && guard_queries==queries && !progress);
 puts("PASS: actual playback-entry guard blocks unavailable/invalid progress, closes allocated session; valid checkpoints preserved.");
 // Many alternating next-opening failures/successes with real concurrent workers.
 for(int i=0;i<1000;i++){reset();fail_source=i%2;int rc=resolve_open_with_animation(42,"Anime",&out,&progress,&fetched);assert(rc==(i%2?-2:0));cJSON_Delete(progress);}
 reset();struct membuf body={.limit=64u*1024u};char chunk[4096]={0};
 for(int i=0;i<16;i++)assert(write_cb(chunk,1,sizeof(chunk),&body)==sizeof(chunk));
 assert(body.len==body.limit && body.cap==body.limit+1 && write_cb(chunk,1,1,&body)==0);membuf_free(&body);
 assert(!active && created==joined && !allocations);
 pthread_t timers[4];for(uintptr_t i=0;i<4;i++)assert(!pthread_create(&timers[i],NULL,timing_worker,(void*)(i+1)));
 for(int i=0;i<4;i++)assert(!pthread_join(timers[i],NULL));
 timing_seconds=NAN;assert(!request_time_ms(NULL,CURLINFO_CONNECT_TIME));timing_seconds=INFINITY;assert(!request_time_ms(NULL,CURLINFO_CONNECT_TIME));
 puts("PASS: diagnostic cumulative times are bounded, consumed once and isolated across 4 concurrent threads.");
 puts("PASS: 1000 transitions, no unjoined thread/JSON leak; real HTTP callback bounds chunked descriptors at 64 KiB.");
}
