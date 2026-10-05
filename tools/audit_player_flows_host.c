/* Real current C functions. No live account, network, SDL renderer or FFmpeg. */
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint32_t Uint32;
typedef struct {int value;} SDL_atomic_t;
static int SDL_AtomicGet(SDL_atomic_t *v){return v->value;}
static void SDL_WaitThread(void *t,void *s){(void)t;(void)s;}
static Uint32 SDL_GetTicks(void){return 100;}
static int jint(const cJSON*j,const char*k){const cJSON*v=cJSON_GetObjectItem(j,k);return cJSON_IsNumber(v)?v->valueint:0;}
static const char *jstr(const cJSON*j,const char*k){const cJSON*v=cJSON_GetObjectItem(j,k);return cJSON_IsString(v)?v->valuestring:NULL;}
static int arr_len(const cJSON*j){return cJSON_GetArraySize(j);}
static cJSON *g_history,*g_history_pending,*g_watchlater_pending;
static void *g_history_thread,*g_watchlater_thread;
static SDL_atomic_t g_history_done,g_watchlater_done;
static int g_history_sel,g_history_zone,g_history_refresh_requested;
static int list_ids[16],list_count;
static cJSON *history_items(void);
static void load_history(void){}
static int store_media_list_create(const char *name){(void)name;return 0;}
static void store_media_list_add(int list,int id,int is_series,const char *title,const char *logo){
  (void)list;(void)is_series;(void)title;(void)logo;
  for(int i=0;i<list_count;i++)if(list_ids[i]==id)return;
  assert(list_count<16);list_ids[list_count++]=id;
}
#include "flow_history.inc"

#define TAB_SAGAS 4
static cJSON *g_land,*g_heroesArr,*g_land_cache[5];
static int g_railsN,g_railItem,g_homeScroll,g_rail_scroll[48],g_heroIdx,g_heroSeriesDefault;
static int g_account_prefs_loaded=1,g_saga_sel,g_railSel;
static Uint32 g_hero_next;
static char g_status[192];
static const char *TAB_NAME[]={"Inicio","Filmes","Series","Animes","Sagas"};
static void *g_land_thread;
static int g_land_queued_tab,starts;
static void load_settings_status(void){}
static void add_rail(const char *name,cJSON *items,int series){(void)name;(void)series;if(arr_len(items))g_railsN++;}
static void hero_pool_add(cJSON *pool,cJSON *items){(void)pool;(void)items;}
static void hero_pool_add_ready(cJSON *pool,cJSON *items){(void)pool;(void)items;}
static void landing_start(int tab){(void)tab;starts++;}
#include "flow_landing.inc"

enum {EXIT_REASON_NATURAL,EXIT_REASON_USER};
typedef struct {int reason,presented_frame;double position,duration;} PlayerResult;
typedef struct {int item_id,saved,completed;} PlaybackSyncStatus;
static int marked,marked_id;
static int api_mark_watched(int id){marked++;marked_id=id;return 0;}
#include "flow_finalize.inc"

#define AVERROR_EOF (-541478725)
static int SDL_GetQueuedAudioSize(int device){(void)device;return 0;}
static void SDL_Delay(int ms){(void)ms;}
static void player_error_text(const char *stage,int error){(void)stage;(void)error;}
static void diag_player_event(const char*a,const char*b,const char*c,...){(void)a;(void)b;(void)c;}
static int classify_terminal(int ret){
  struct {int cancelled;} open_watch={0};
  int running=1,adev=0,native_hls=1,logged_first_present=1,reached_end=0,playback_error=0;
  while(running){
#include "flow_terminal.inc"
  }
  return playback_error?playback_error:reached_end;
}
static void set_history(const char *json){if(g_history)cJSON_Delete(g_history);g_history=cJSON_Parse(json);assert(g_history);g_history_sel=1;g_history_zone=0;}
int main(void){
  set_history("{\"items\":[{\"item_id\":10},{\"item_id\":20}]}");
  int opened_menu_item=jint(cJSON_GetArrayItem(history_items(),g_history_sel),"item_id");
  g_history_pending=cJSON_Parse("{\"items\":[{\"item_id\":30},{\"item_id\":10},{\"item_id\":20}]}");
  pump_history();
  int applied_menu_item=jint(cJSON_GetArrayItem(history_items(),g_history_sel),"item_id");
  assert(opened_menu_item==20&&applied_menu_item==10);
  printf("REPRODUCED history reorder: menu opened for %d; same selected index now targets %d.\n",opened_menu_item,applied_menu_item);

  g_history_pending=cJSON_Parse("{\"error\":\"unexpected payload\"}");pump_history();
  assert(arr_len(history_items())==0&&g_history_zone==1);
  puts("REPRODUCED malformed history: parseable response without items erases known history.");

  list_count=2;list_ids[0]=10;list_ids[1]=20;
  g_watchlater_pending=cJSON_Parse("{\"items\":[{\"item_id\":10,\"title\":\"Keep\"}]}");pump_history();
  assert(list_count==2&&list_ids[1]==20);
  puts("REPRODUCED watchlater merge: item removed remotely remains in the local synchronized list.");

  cJSON *catalog=cJSON_Parse("{\"continue\":[{\"id\":10},{\"id\":20}],\"heroes\":[{\"id\":30}]}");assert(catalog);
  g_land_cache[0]=catalog;g_railSel=3;g_railItem=7;g_homeScroll=800;g_heroIdx=2;g_rail_scroll[3]=500;
  load_landing(0);
  assert(g_railSel==-1&&g_railItem==0&&g_homeScroll==0&&g_heroIdx==0&&g_rail_scroll[3]==0);
  puts("REPRODUCED cached tab return: rail/item/scroll/hero selection reset to the beginning.");
  g_land_thread=(void *)(uintptr_t)1;g_land_queued_tab=2;load_landing(0);
  assert(g_land_queued_tab==2&&starts==0);
  puts("REPRODUCED cached tab switch: old queued tab remains despite the latest cached selection (unnecessary fetch).");

  PlayerResult result={EXIT_REASON_NATURAL,1,300,1800};PlaybackSyncStatus sync={0};
  assert(classify_terminal(AVERROR_EOF)==1);
  assert(finalize_natural_playback(99,&result,&sync)==1&&marked==1&&marked_id==99);
  puts("REPRODUCED premature EOF policy: known 30-minute content stopped at 5 minutes is marked watched when decoder reports EOF.");
  assert(classify_terminal(-5)==-5);
  result.reason=EXIT_REASON_USER;assert(!finalize_natural_playback(99,&result,&sync)&&marked==1);
  puts("CONTROL: explicit network error and user exit do not take that natural-completion path.");
  cJSON_Delete(catalog);g_land=g_heroesArr=NULL;g_land_cache[0]=NULL;
  cJSON_Delete(g_history);g_history=NULL;
  return 0;
}
