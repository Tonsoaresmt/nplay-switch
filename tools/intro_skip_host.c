#define _POSIX_C_SOURCE 200809L
#include "intro_skip.h"
#include "player_ui.h"
#include "net.h"
#include "api.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

struct SDL_Thread {pthread_t id;int(*fn)(void*);void *arg;};
static pthread_t owner;
static int created,joined,fail_create,mode,api_calls,remote_calls;
static SDL_atomic_t ticks,delay_worker,entered;
const char *BASE="https://nplay.test";
char g_token[640];
int SDL_AtomicGet(SDL_atomic_t*a){return __atomic_load_n(&a->value,__ATOMIC_SEQ_CST);}
int SDL_AtomicSet(SDL_atomic_t*a,int v){return __atomic_exchange_n(&a->value,v,__ATOMIC_SEQ_CST);}
Uint32 SDL_GetTicks(void){return (Uint32)SDL_AtomicGet(&ticks);}
static void nap(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static void *worker(void *p){SDL_Thread*t=p;t->fn(t->arg);return NULL;}
SDL_Thread *SDL_CreateThread(int(*fn)(void*),const char*name,void*arg){
 assert(pthread_equal(owner,pthread_self())&&!strcmp(name,"intro-metadata"));if(fail_create)return NULL;
 SDL_Thread*t=calloc(1,sizeof(*t));assert(t);t->fn=fn;t->arg=arg;created++;assert(!pthread_create(&t->id,NULL,worker,t));return t;
}
void SDL_WaitThread(SDL_Thread*t,int*out){assert(pthread_equal(owner,pthread_self()));assert(!pthread_join(t->id,NULL));if(out)*out=0;joined++;free(t);}
const char *jstr(cJSON*j,const char*k){cJSON*v=cJSON_GetObjectItemCaseSensitive(j,k);return cJSON_IsString(v)?v->valuestring:NULL;}
void membuf_free(struct membuf*m){free(m->data);memset(m,0,sizeof(*m));}
static void body(struct membuf*m,const char*s){m->len=strlen(s);m->data=malloc(m->len+1);assert(m->data);memcpy(m->data,s,m->len+1);}
long net_request_timeout_cancel(const char*u,const char*m,const char*b,const char*token,struct membuf*out,const char**err,long conn,long total,SDL_atomic_t*cancel){
 assert(!pthread_equal(owner,pthread_self()));assert(!strcmp(m,"GET")&&!b&&!strcmp(token,"fixture-token"));
 assert(!strcmp(u,"https://nplay.test/api/stream/42/segments?duration=1440.000&source_id=7"));
 assert(out->limit==32768&&conn==2&&total==5);api_calls++;
 SDL_AtomicSet(&entered,1);
 while(SDL_AtomicGet(&delay_worker)&&!SDL_AtomicGet(cancel))nap();
 *err=NULL;if(SDL_AtomicGet(cancel))return -42;
 if(mode==4)return -28;
 if(mode==5)return 401;
 if(mode==1)body(out,"{\"segments\":[{\"type\":\"intro\",\"start\":10,\"end\":90},{\"type\":\"outro\",\"start\":1300,\"end\":1435}]}");
 else if(mode==6){body(out,"{}");out->len=32769;}
 else body(out,"{\"segments\":[],\"fallback_lookup\":{\"provider\":\"theintrodb\",\"tmdb_id\":123,\"season\":2,\"episode\":4}}");
 return 200;
}
long net_get_text_limited(const char*u,size_t limit,long timeout,SDL_atomic_t*cancel,struct membuf*out){
 assert(!strcmp(u,"https://api.theintrodb.org/v3/media?tmdb_id=123&season=2&episode=4&duration_ms=1440000"));
 assert(limit==32768&&timeout>0&&timeout<=3000&&!SDL_AtomicGet(cancel));remote_calls++;
 if(mode==7)return 503;
 if(mode==3)body(out,"{\"type\":\"tv\",\"tmdb_id\":999,\"season\":2,\"episode\":4,\"intro\":[{\"start_ms\":10000,\"end_ms\":90000}]}");
 else body(out,"{\"type\":\"tv\",\"tmdb_id\":123,\"season\":2,\"episode\":4,\"intro\":[{\"start_ms\":10000,\"end_ms\":90000}]}");
 return 200;
}
void diag_network_event(const char*m,const char*p,long c,unsigned elapsed,size_t n){(void)c;(void)elapsed;(void)n;assert(!strcmp(m,"GET")&&!strcmp(p,"/api/stream/42/segments"));}
static void wait_job(IntroSkip*j){for(int i=0;i<10000&&!SDL_AtomicGet(&j->done);i++)nap();assert(SDL_AtomicGet(&j->done));}
static void metadata(void){
 for(int round=0;round<30;round++)for(mode=1;mode<=7;mode++){
  IntroSkip j={0};intro_skip_poll(&j,42,7,1440,"fixture-token");assert(j.thread);
  wait_job(&j);intro_skip_poll(&j,42,7,1440,"fixture-token");assert(!j.thread&&!j.bearer[0]);
  assert(j.ready.count==(mode==1?2:mode==2?1:0));
  int calls=api_calls;for(int i=0;i<1000;i++)intro_skip_poll(&j,42,7,1440,"fixture-token");assert(calls==api_calls);
  const IntroSegment*r=intro_skip_current(&j,20,1440,1);
  assert((r!=NULL)==(mode<=2));assert(!intro_skip_current(&j,20,1440,0));
  assert(!intro_skip_current(&j,20,1400,1));assert(!intro_skip_current(&j,NAN,1440,1));
  intro_skip_stop(&j);assert(!j.thread&&!j.ready.count);
 }
 mode=1;IntroSkip j={0};SDL_AtomicSet(&delay_worker,1);SDL_AtomicSet(&entered,0);
 intro_skip_poll(&j,42,7,1440,"fixture-token");while(!SDL_AtomicGet(&entered))nap();
 // Cancel own metadata transport, not the demux/video. Worker released by cancel.
 intro_skip_stop(&j);assert(!j.thread&&!j.ready.count);SDL_AtomicSet(&delay_worker,0);
 intro_skip_poll(&j,42,7,1440,"fixture-token");wait_job(&j);
 intro_skip_poll(&j,99,8,1440,"fixture-token");assert(!j.ready.count);intro_skip_stop(&j);
 fail_create=1;intro_skip_poll(&j,42,7,1440,"fixture-token");assert(!j.thread&&!j.bearer[0]);intro_skip_stop(&j);fail_create=0;
 int calls=created;intro_skip_poll(&j,0,7,1440,"fixture-token");intro_skip_poll(&j,42,7,2,"fixture-token");intro_skip_poll(&j,42,7,1440,NULL);assert(created==calls);
 assert(created==joined);
}
static void parsing(void){
 IntroSegments out;
 assert(intro_skip_parse("{\"segments\":[{\"type\":\"intro\",\"start\":10,\"end\":90}]}",1440,&out)==1);
 const char*bad[]={NULL,"","{}","[]","null","{\"segments\":[{\"type\":\"intro\",\"start\":\"10\",\"end\":90}]}",
 "{\"segments\":[{\"type\":\"intro\",\"start\":-1,\"end\":90}]}",
 "{\"segments\":[{\"type\":\"intro\",\"start\":10,\"end\":900}]}",
 "{\"segments\":[{\"type\":\"intro\",\"start\":10,\"end\":90},{\"type\":\"intro\",\"start\":15,\"end\":95}]}",
 "{\"segments\":[{\"type\":\"outro\",\"start\":10,\"end\":90}]}"};
 for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!intro_skip_parse(bad[i],1440,&out));
 assert(!intro_skip_parse("{}",NAN,&out));assert(!intro_skip_parse("{}",INFINITY,&out));
 IntroSkip j={.duration=1440,.ready={.count=1,.ranges={{1,10,90}}}};
 assert(!intro_skip_current(&j,9.99,1440,1));assert(intro_skip_current(&j,10,1440,1));assert(!intro_skip_current(&j,89.6,1440,1));
 j.ready.ranges[0]=(IntroSegment){2,1300,1440};assert(intro_skip_target(&j.ready.ranges[0],1440)==1439);
 assert(intro_skip_current(&j,1300,1440,1));assert(!intro_skip_current(&j,1438.6,1440,1));
}

enum {JOY_A=0,JOY_B=1,JOY_DLEFT=12};
static int intro_selected,track_menu,timeline_seek,episodes_menu,next_selected,paused;
static int timeline_seek_was_paused;
static double cur_pos,timeline_seek_from,timeline_seek_target,dur=1440;
static const IntroSegment *skip_range;
static void diag_player_event(const char*a,const char*b,const char*c,...){assert(!strcmp(a,"intro")&&!strcmp(b,"skip-confirmed"));(void)c;}
static void activate(int b){for(int once=0;once<1;once++){
#include "intro_activate.inc"
}}
static int intro_touch_active;static SDL_FingerID intro_touch_finger;
#define PWIN_W 1280
#define PWIN_H 720
static int touch(SDL_Event e){int touch_track_button=-1;for(int once=0;once<1;once++){
#include "intro_touch.inc"
}return touch_track_button;}
static SDL_Event finger(int type,SDL_FingerID id,int x,int y){SDL_Event e={0};e.type=(unsigned)type;e.tfinger.fingerId=id;e.tfinger.x=x/1280.f;e.tfinger.y=y/720.f;return e;}
enum{ST_NORMAL,ST_SMALL};
static const SDL_Color K_ACC={0},K_PANEL={0},K_WHITE={0},K_MUTED={0};
static int shapes,labels;
static void rrect(SDL_Renderer*r,float x,float y,float w,float h,float radius,SDL_Color c,float a){(void)r;(void)radius;(void)c;(void)a;assert(x>=INTRO_BUTTON_X&&x+w<=INTRO_BUTTON_X+INTRO_BUTTON_W&&y>=INTRO_BUTTON_Y&&y+h<=INTRO_BUTTON_Y+INTRO_BUTTON_H);shapes++;}
static void text_center_a(SDL_Renderer*r,const char*s,int x,int y,SDL_Color c,int style,float a){(void)r;(void)c;(void)style;(void)a;assert(s&&x==640&&y>=INTRO_BUTTON_Y&&y<INTRO_BUTTON_Y+INTRO_BUTTON_H);labels++;}
#include "intro_draw.inc"
static void controls(void){
 IntroSegment range={1,10,90};skip_range=&range;cur_pos=20;paused=1;
 intro_selected=1;activate(JOY_A);assert(timeline_seek&&timeline_seek_target==90&&timeline_seek_from==20&&timeline_seek_was_paused&&paused);
 timeline_seek=0;intro_selected=1;activate(JOY_B);assert(!timeline_seek&&!intro_selected&&paused);
 intro_selected=0;activate(JOY_A);assert(!timeline_seek);intro_selected=1;track_menu=1;activate(JOY_A);assert(!timeline_seek);track_menu=0;intro_selected=0;
 assert(2*INTRO_BUTTON_X+INTRO_BUTTON_W==1280&&INTRO_BUTTON_Y+INTRO_BUTTON_H<600);
 assert(intro_skip_hit(640,540)&&!intro_skip_hit(INTRO_BUTTON_X-1,540)&&!intro_skip_hit(640,INTRO_BUTTON_Y+INTRO_BUTTON_H));
 assert(touch(finger(SDL_FINGERDOWN,1,640,540))==-1&&intro_touch_active);
 assert(touch(finger(SDL_FINGERUP,2,640,540))==-1&&intro_touch_active);
 assert(touch(finger(SDL_FINGERUP,1,640,540))==JOY_A&&!intro_touch_active&&intro_selected);
 intro_selected=0;touch(finger(SDL_FINGERDOWN,1,640,540));touch(finger(SDL_FINGERMOTION,1,40,540));assert(!intro_touch_active);assert(touch(finger(SDL_FINGERUP,1,640,540))==-1);
 touch(finger(SDL_FINGERDOWN,1,640,540));skip_range=NULL;assert(touch(finger(SDL_FINGERUP,1,640,540))==-1&&!intro_selected);skip_range=&range;
 PlayerHud h={.intro_visible=1};draw_intro_skip(NULL,&h);assert(shapes==1&&labels==2);
 h.panel_open=1;draw_intro_skip(NULL,&h);assert(shapes==1);h.panel_open=0;h.scrubbing=1;draw_intro_skip(NULL,&h);assert(shapes==1);
 h.scrubbing=0;h.intro_selected=1;draw_intro_skip(NULL,&h);assert(shapes==3&&labels==4);
}
int main(void){owner=pthread_self();parsing();metadata();controls();printf("Intro skip: %d joined pthread requests; strict ranges, bounded optional API/provider, cancellation/stale results, pause-preserving confirmation, touch capture and centered draw geometry passed. No physical Switch/pixel/provider test.\n",created);return 0;}
