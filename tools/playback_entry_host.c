#include "api.h"
#include "playback_entry.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {SC_LOGIN=0,SC_SERIES=1};
static int g_running,g_screen,resolve_calls,play_calls,prompts,events,allocations;
static int codes[8],answers[8],cancel_resolve,consume_progress,login_on_error,quit_on_error;
static void *tracked_malloc(size_t n){void*p=malloc(n);if(p)allocations++;return p;}
static void tracked_free(void*p){if(p){allocations--;free(p);}}
static int resolve_open_with_animation(int id,const char*title,PlaybackSource*out,cJSON**progress,int*fetched){
 assert(id==42&&!strcmp(title,"T3 E2"));assert(resolve_calls<8);resolve_calls++;
 memset(out,0,sizeof(*out));out->item_id=id;out->source_id=7;
 *progress=cJSON_Parse("{\"progress\":{\"position_seconds\":120,\"completed\":false}}");
 assert(*progress);*fetched=1;return cancel_resolve?-2:0;
}
static int resolve_and_play_resolved(int id,char*title,char*subtitle,char*overview,char*next,int has_next,
 PlaybackSource*source,cJSON**progress,int fetched){
 assert(id==42&&source->item_id==42&&source->source_id==7&&fetched&&*progress);
 assert(!strcmp(title,"T3 E2")&&!strcmp(subtitle,"Episode 2")&&!strcmp(overview,"Plot")&&!strcmp(next,"Episode 3")&&has_next);
 assert(play_calls<8);int rc=codes[play_calls++];
 if(consume_progress){cJSON_Delete(*progress);*progress=NULL;}
 if(rc==PLAYBACK_ENTRY_FAILED){if(login_on_error)g_screen=SC_LOGIN;if(quit_on_error)g_running=0;}
 return rc;
}
static int prompt_playback_retry(const char*title){assert(g_running&&g_screen!=SC_LOGIN&&!strcmp(title,"T3 E2"));assert(prompts<8);return answers[prompts++];}
static void diag_player_event(const char*group,const char*event,const char*fmt,...){assert(!strcmp(group,"episodes"));assert(!strcmp(event,"opening-failed")||!strcmp(event,"opening-retry"));(void)fmt;events++;}
#include "playback_entry_actual.inc"
static void reset(void){assert(!allocations);g_running=1;g_screen=SC_SERIES;resolve_calls=play_calls=prompts=events=0;
 memset(codes,0,sizeof(codes));memset(answers,0,sizeof(answers));cancel_resolve=consume_progress=login_on_error=quit_on_error=0;}
static int run(void){int rc=resolve_and_play_details(42,"T3 E2","Episode 2","Plot","Episode 3",1);assert(!allocations);return rc;}
int main(void){
 cJSON_Hooks hooks={tracked_malloc,tracked_free};cJSON_InitHooks(&hooks);
 for(int consumed=0;consumed<2;consumed++){
  reset();consume_progress=consumed;codes[0]=PLAYBACK_ENTRY_FAILED;codes[1]=2;answers[0]=1;
  assert(run()==2&&resolve_calls==2&&play_calls==2&&prompts==1);
  reset();consume_progress=consumed;codes[0]=codes[1]=PLAYBACK_ENTRY_FAILED;codes[2]=1;answers[0]=answers[1]=1;
  assert(run()==1&&resolve_calls==3&&play_calls==3&&prompts==2);
 }
 reset();codes[0]=PLAYBACK_ENTRY_FAILED;assert(!run()&&resolve_calls==1&&prompts==1);
 reset();assert(!run()&&resolve_calls==1&&!prompts);
 reset();cancel_resolve=1;assert(!run()&&!play_calls&&!prompts);
 reset();codes[0]=PLAYBACK_ENTRY_FAILED;login_on_error=1;assert(!run()&&!prompts);
 reset();codes[0]=PLAYBACK_ENTRY_FAILED;quit_on_error=1;assert(!run()&&!prompts);
 reset();codes[0]=1;assert(run()==1&&!prompts);
 reset();codes[0]=2;assert(run()==2&&!prompts);
 for(int i=0;i<1000;i++){reset();consume_progress=i%2;codes[0]=PLAYBACK_ENTRY_FAILED;answers[0]=i%2;codes[1]=2;
  assert(run()==(i%2?2:0)&&resolve_calls==(i%2?2:1));}
 puts("PLAYBACK ENTRY OK: exact episode retained, explicit retry only after exhausted recovery, cancel/auth/quit distinguished, natural/manual next need no confirmation, 1000 ownership cycles. API/player/SDL simulated.");
}
