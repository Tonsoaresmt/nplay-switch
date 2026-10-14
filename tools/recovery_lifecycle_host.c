typedef struct SDL_Joystick SDL_Joystick;
#include "player.h"
#include "player_lifecycle.h"
#include "net.h"
#include <switch.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define JOY_B 1
#define JOY_MINUS 11
struct SDL_Thread { pthread_t id; int (*fn)(void *); void *arg; };
static AppletHookFn hook;static void *hook_arg;
static int mode,event_sent,joins,draws;
static char error_text[192];
const char *BASE="https://fixture.test";char g_token[640]="fixture-token";
int SDL_AtomicGet(SDL_atomic_t *a){return __atomic_load_n(&a->value,__ATOMIC_SEQ_CST);}
int SDL_AtomicSet(SDL_atomic_t *a,int v){return __atomic_exchange_n(&a->value,v,__ATOMIC_SEQ_CST);}
Uint32 SDL_GetTicks(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (Uint32)(t.tv_sec*1000u+t.tv_nsec/1000000);}
void SDL_Delay(Uint32 ms){struct timespec t={ms/1000,(long)(ms%1000)*1000000L};nanosleep(&t,NULL);}
void appletHook(AppletHookCookie*c,AppletHookFn f,void*p){(void)c;hook=f;hook_arg=p;}
void appletUnhook(AppletHookCookie*c){(void)c;hook=NULL;}
AppletFocusState appletGetFocusState(void){return AppletFocusState_InFocus;}
static void *thread_entry(void *arg){SDL_Thread*t=arg;t->fn(t->arg);return NULL;}
SDL_Thread *SDL_CreateThread(int(*fn)(void*),const char *name,void*arg){
 assert(!strcmp(name,"player-recovery"));if(mode==5)return NULL;
 SDL_Thread*t=malloc(sizeof(*t));assert(t);t->fn=fn;t->arg=arg;assert(!pthread_create(&t->id,NULL,thread_entry,t));return t;
}
void SDL_WaitThread(SDL_Thread*t,int*rc){(void)rc;assert(!pthread_join(t->id,NULL));joins++;free(t);if(mode==8)hook(AppletHookType_OnResume,hook_arg);}
int SDL_PollEvent(SDL_Event *e){
 if(event_sent)return 0;
 event_sent=1;
 if(mode==1){hook(AppletHookType_OnResume,hook_arg);return 0;}
 if(mode==2){e->type=SDL_JOYBUTTONDOWN;e->jbutton.button=JOY_B;return 1;}
 if(mode==6){e->type=SDL_QUIT;return 1;}
 if(mode==7){e->type=SDL_FINGERDOWN;e->tfinger.x=.5f;e->tfinger.y=.9f;return 1;}
 return 0;
}
static int SDL_JoystickGetButton(SDL_Joystick*j,int b){(void)j;(void)b;return 0;}
static void SDL_FlushEvent(unsigned type){assert(type==SDL_JOYBUTTONDOWN);}
void SDL_RenderPresent(SDL_Renderer*r){(void)r;draws++;}
static void pui_draw_loading(SDL_Renderer*r,const char*t,const char*h,const char*d,Uint32 at,int cancel){(void)r;(void)t;(void)h;(void)d;(void)at;assert(cancel);}
static void player_error_message(const char*m){snprintf(error_text,sizeof(error_text),"%s",m);}
void diag_network_event(const char*m,const char*p,long c,unsigned ms,size_t b){(void)m;(void)p;(void)c;(void)ms;(void)b;}
long net_request_timeout_cancel(const char*u,const char*m,const char*b,const char*t,struct membuf*out,const char**err,long c,long a,SDL_atomic_t*cancel){
 (void)u;(void)m;(void)b;(void)t;(void)c;(void)a;(void)cancel;*err=NULL;
 const char*s="{\"error\":\"Acesso expirado\",\"reason\":\"expired\"}";out->len=strlen(s);out->data=malloc(out->len+1);assert(out->data);memcpy(out->data,s,out->len+1);return 401;
}
void membuf_free(struct membuf*b){free(b->data);b->data=NULL;}
long net_request_timeout(const char*u,const char*m,const char*b,const char*t,struct membuf*out,const char**err,long c,long a){
 return net_request_timeout_cancel(u,m,b,t,out,err,c,a,NULL);
}
#include "recovery_lifecycle.inc"
static int callback(const PlaybackSource*a,PlaybackSource*b,SDL_atomic_t*c,void*u){
 (void)u;SDL_Delay(30);
 if(mode==3)return -3;
 if(mode==4)return api_refresh_playback_cancel(a,c,b);
 // Deliberately simulate a late response that ignores cancellation.
 *b=*a;b->session_id=78;snprintf(b->play_url,sizeof(b->play_url),"https://fixture.test/new");return 0;
}
int main(void){
 PlaybackSource a={.item_id=42,.session_id=77,.delivery=DELIVERY_R2};
 snprintf(a.play_url,sizeof(a.play_url),"https://fixture.test/old");
 const int expected[]={0,2,1,-3,-2,-1,1,1,2};
 for(int cycle=0;cycle<25;cycle++)for(mode=0;mode<9;mode++){
  player_lifecycle_begin();event_sent=draws=0;PlaybackSource b={.session_id=991};
  int before=joins;int rc=player_recovery_call(NULL,NULL,"Fixture","Retomando","Teste",callback,&a,&b,NULL);
  assert(rc==expected[mode]);assert(joins==before+(mode!=5));
  assert(mode==5||draws>0);assert(b.session_id==(mode?991:78));
  if(mode==4)assert(strstr(error_text,"Acesso expirado"));
  player_lifecycle_end();assert(!hook);
 }
 puts("PASS actual recovery UI/worker + pthread: 225 resolutions, late HOME response discarded after join, B/touch/quit cancel, terminal and expired denials, thread OOM, wake at join; no dangling output.");
}
