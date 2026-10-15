typedef struct SDL_Joystick SDL_Joystick;
#include "player.h"
#include "player_sync.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
typedef int32_t Sint32;
static Uint32 ticks, ready_at, stop_at, cancelled_at;
static SDL_atomic_t *running_flag,*ready_flag,*cancel_flag;
int SDL_AtomicGet(SDL_atomic_t *v){return v->value;}
int SDL_AtomicSet(SDL_atomic_t *v,int n){int old=v->value;v->value=n;return old;}
void SDL_AtomicLock(int *v){while(__atomic_exchange_n(v,1,__ATOMIC_ACQUIRE))sched_yield();}
void SDL_AtomicUnlock(int *v){assert(__atomic_load_n(v,__ATOMIC_RELAXED));__atomic_store_n(v,0,__ATOMIC_RELEASE);}
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
#ifdef HAS_STARTED_GENERATION
 hb.started_generation=1;
#endif
 hb.started_source.item_id=42;hb.started_source.request_id=91;
 snprintf(hb.started_source.hot_session_id,sizeof(hb.started_source.hot_session_id),"1234567890abcdef_safe-session");
 running_flag=&hb.running;ready_flag=&hb.pipeline_ready;cancel_flag=&hb.cancel_io;
 hb.callback_userdata=ready_flag;ticks=0;calls=0;failures=fails;
 ready_at=ready;stop_at=stop;cancelled_at=cancel;
 assert(!playback_heartbeat_thread(&hb) && hb.finished.value);
}
static Uint32 beats[16];static int beat_count;
static int slow_progress(int item,int pos,int duration,SDL_atomic_t *cancel,void *u){
 (void)cancel;(void)u;assert(item==42&&pos==600&&duration==1440);ticks+=10000;return 0;
}
static int heartbeat(int session,SDL_atomic_t *cancel,void *u){
 (void)cancel;(void)u;assert(session==77&&beat_count<16);beats[beat_count++]=ticks;return 0;
}
#ifdef HAS_STARTED_GENERATION
static PlaybackHeartbeat *mailbox;static int mailbox_calls,mailbox_phase;
static void *publish_renewed(void *unused){
 (void)unused;
 while(__atomic_load_n(&mailbox_phase,__ATOMIC_ACQUIRE)!=1)sched_yield();
 SDL_AtomicLock(&mailbox->started_lock);
 mailbox->started_source.request_id=92;mailbox->started_generation++;
 SDL_AtomicUnlock(&mailbox->started_lock);
 __atomic_store_n(&mailbox_phase,2,__ATOMIC_RELEASE);return NULL;
}
static int renewed_started(const PlaybackSource *source,SDL_atomic_t *cancel,void *u){
 (void)cancel;(void)u;mailbox_calls++;
 if(mailbox_calls==1){
  assert(source->request_id==91);
  __atomic_store_n(&mailbox_phase,1,__ATOMIC_RELEASE);
  while(__atomic_load_n(&mailbox_phase,__ATOMIC_ACQUIRE)!=2)sched_yield();
  assert(source->request_id==91); // Immutable borrowed callback snapshot.
 }else assert(source->request_id==92&&mailbox_calls==2);
 return 0;
}
#endif
int main(void){
 run(0,30000,0,0);assert(!calls); // Never displayed a frame.
 run(500,30000,0,0);assert(calls==1 && called_at[0]==500);
 run(500,30000,1,0);assert(calls==2 && called_at[1]>=called_at[0]+2000);
 run(500,30000,20,0);assert(calls==3 && called_at[2]>=called_at[1]+4000);
 run(500,30000,0,100);assert(!calls); // Cancellation prevents starting new work.
 PlaybackHeartbeat hb={0};hb.running.value=hb.pipeline_ready.value=1;
 hb.item_id=42;hb.session_id.value=77;hb.current_pos.value=600;hb.duration.value=1440;
 hb.progress_cb=slow_progress;hb.heartbeat_cb=heartbeat;
 running_flag=&hb.running;ready_flag=&hb.pipeline_ready;cancel_flag=&hb.cancel_io;
 ticks=ready_at=cancelled_at=0;stop_at=90000;beat_count=0;
 playback_heartbeat_thread(&hb);
 assert(beat_count>=3&&beats[0]<=26000);
 for(int i=1;i<beat_count;i++)assert(beats[i]-beats[i-1]<=30000);
 puts("PASS real heartbeat worker: slow 10s progress callback counts toward elapsed time, bounded cadence without catch-up bursts.");
#ifdef HAS_STARTED_GENERATION
 for(int i=0;i<100;i++){
  PlaybackHeartbeat fresh={0};fresh.running.value=fresh.pipeline_ready.value=1;
  fresh.started_cb=renewed_started;fresh.started_generation=1;fresh.started_source.request_id=91;mailbox=&fresh;
  running_flag=&fresh.running;ready_flag=&fresh.pipeline_ready;cancel_flag=&fresh.cancel_io;
  ticks=ready_at=cancelled_at=0;stop_at=5000;mailbox_calls=0;mailbox_phase=0;
  pthread_t producer;assert(!pthread_create(&producer,NULL,publish_renewed,NULL));
  playback_heartbeat_thread(&fresh);assert(!pthread_join(producer,NULL));
  assert(mailbox_calls==2&&!fresh.started_lock);
 }
 puts("PASS started mailbox: 100 pthread publications during old callback; new request confirmed once, immutable descriptor, no network under lock.");
#endif
 puts("PLAYBACK STARTED OK: real worker, presentation gate, once-only success, bounded retries and immutable SID; SDL/time simulated.");
}
