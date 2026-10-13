typedef struct SDL_Joystick SDL_Joystick;
#include "player.h"
#include "player_sync.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef int32_t Sint32;
static Uint32 ticks, ready_at, stop_at, cancelled_at;
static SDL_atomic_t *running_flag,*ready_flag,*cancel_flag;
int SDL_AtomicGet(SDL_atomic_t *v){return v->value;}
int SDL_AtomicSet(SDL_atomic_t *v,int n){int old=v->value;v->value=n;return old;}
static int SDL_AtomicCAS(SDL_atomic_t*v,int old,int n){if(v->value!=old)return 0;v->value=n;return 1;}
Uint32 SDL_GetTicks(void){return ticks;}
void SDL_Delay(Uint32 ms){ticks+=ms;if(ready_at && ticks>=ready_at)ready_flag->value=1;
 if(cancelled_at && ticks>=cancelled_at)cancel_flag->value=1;
 if(ticks>=stop_at)running_flag->value=0;}
#include "playback_started.inc"
static int calls,failures;
static Uint32 called_at[4];
static int started(const PlaybackSource*s,SDL_atomic_t*cancel,void*u){
 assert(u==(void*)ready_flag && ready_flag->value && ticks>=ready_at);
 assert(s->request_id==91 && s->item_id==42 && s->session_id==0);
 assert(!strcmp(s->hot_session_id,"1234567890abcdef_safe-session"));
 called_at[calls++]=ticks;
 if(SDL_AtomicGet(cancel))return -1;
 return calls<=failures ? -1 : 0;
}
static void run(Uint32 ready,Uint32 stop,int fails,int cancel){
 PlaybackHeartbeat hb={0};hb.running.value=1;hb.started_cb=started;
 hb.started_source.item_id=42;hb.started_source.request_id=91;
 snprintf(hb.started_source.hot_session_id,sizeof(hb.started_source.hot_session_id),"1234567890abcdef_safe-session");
 running_flag=&hb.running;ready_flag=&hb.pipeline_ready;cancel_flag=&hb.cancel_io;
 hb.callback_userdata=ready_flag;ticks=0;calls=0;failures=fails;
 ready_at=ready;stop_at=stop;cancelled_at=cancel;
 assert(!playback_heartbeat_thread(&hb) && hb.finished.value);
}
int main(void){
 run(0,30000,0,0);assert(!calls); // Never displayed a frame.
 run(500,30000,0,0);assert(calls==1 && called_at[0]==500);
 run(500,30000,1,0);assert(calls==2 && called_at[1]>=called_at[0]+2000);
 run(500,30000,20,0);assert(calls==3 && called_at[2]>=called_at[1]+4000);
 run(500,30000,0,100);assert(!calls); // Cancellation prevents starting new work.
 puts("PLAYBACK STARTED OK: real worker, presentation gate, once-only success, bounded retries and immutable SID; SDL/time simulated.");
}
