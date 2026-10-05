#include "cJSON.h"
#include "player_touch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint32_t Uint32;
static Uint32 SDL_GetTicks(void){return 100;}
static int jint(const cJSON*j,const char*k){const cJSON*v=cJSON_GetObjectItem(j,k);return cJSON_IsNumber(v)?v->valueint:0;}
static const char*jstr(const cJSON*j,const char*k){const cJSON*v=cJSON_GetObjectItem(j,k);return cJSON_IsString(v)?v->valuestring:NULL;}
static int arr_len(const cJSON*j){return cJSON_GetArraySize(j);}
#define TAB_SAGAS 4
#define TAB_DOWNLOADS 5
static cJSON *g_land,*g_heroesArr,*g_land_cache[5];
typedef struct {char label[48];cJSON *arr;int count,is_series;} Rail;
static Rail g_rails[48];
static int g_railsN,g_railItem,g_homeScroll,g_rail_scroll[48],g_heroIdx,g_heroSeriesDefault;
static Uint32 g_hero_next;
#include "navigation_types.inc"
static int g_account_prefs_loaded=1,g_saga_sel,g_railSel;
static char g_status[192];
static const char *TAB_NAME[]={"Inicio","Filmes","Series","Animes","Sagas"};
static void *g_land_thread;
static int g_land_queued_tab,starts;
static void load_settings_status(void){}
static void landing_start(int tab){(void)tab;starts++;}
#include "navigation_landing.inc"
static int g_tab,g_dlSel,g_dlScroll,g_dlView,g_history_sel,g_history_zone,g_list_sel;
static int g_history_scroll,g_media_list_scroll,g_list_grid_scroll,g_history_menu,g_history_menu_sel,g_history_menu_item_id;
static int g_history_context_initialized;
static Uint32 g_dl_next;
static cJSON *history_items(void){return NULL;}
static void local_dl_refresh(void){}
static void load_downloads(void){}
static void load_history(void){}
#include "navigation_enter.inc"
#define PWIN_W 1280
#define PWIN_H 720
enum{SDL_FINGERDOWN=1,SDL_FINGERMOTION,SDL_FINGERUP,JOY_A=10,JOY_B,JOY_DLEFT,JOY_L,JOY_R};
typedef int64_t SDL_FingerID;
static SDL_FingerID modal_finger;
static int modal_touch_active,modal_touch_kind,modal_touch_hit,modal_touch_row,modal_touch_drag;
static float modal_touch_y,modal_touch_motion;
static int timeline_seek,episodes_menu,episodes_sel,episode_count;
static int track_menu,have_video_frame=1;
static int sequential_stream;
static double dur=1800,timeline_seek_target;
static int paused,timeline_seek_was_paused;
static void SDL_PauseAudioDevice(int d,int p){(void)d;(void)p;}
static void preview(int tx,int initial_pause){
 struct{struct{SDL_FingerID fingerId;float y;}tfinger;}e={{1,600/720.f}};
 int adev=0;paused=initial_pause;
#include "navigation_preview.inc"
}
static int touch(int type,int x,int y,SDL_FingerID id){
 struct {int type;struct{float x,y;SDL_FingerID fingerId;}tfinger;}e={type,{x/1280.f,y/720.f,id}};
 int touch_track_button=-1;
 for(int once=0;once<1;once++){
#include "navigation_touch.inc"
 }
 return touch_track_button;
}
static int tap(int x,int y){assert(touch(SDL_FINGERDOWN,x,y,1)==-1);return touch(SDL_FINGERUP,x,y,1);}
int main(void){
 g_land_cache[0]=cJSON_Parse("{\"heroes\":[{\"id\":70},{\"id\":80}],\"recentMovies\":[{\"id\":10},{\"id\":20},{\"id\":30}]}");
 g_land_cache[1]=cJSON_Parse("{\"prontos\":[{\"id\":90},{\"id\":91}]}");assert(g_land_cache[0]&&g_land_cache[1]);
 load_landing(0);assert(g_railsN==1);g_railSel=0;g_railItem=1;g_homeScroll=312;g_rail_scroll[0]=111;g_heroIdx=1;
 landing_capture_focus();load_landing(1);g_railSel=0;g_railItem=1;landing_capture_focus();load_landing(0);
 assert(g_railSel==0&&g_railItem==1&&g_homeScroll==312&&g_rail_scroll[0]==111&&g_heroIdx==1);
 cJSON *fresh=cJSON_Parse("{\"heroes\":[{\"id\":80},{\"id\":70}],\"recentMovies\":[{\"id\":30},{\"id\":10},{\"id\":20}]}" );assert(fresh);
 landing_capture_focus();g_land=NULL;g_heroesArr=NULL;cJSON_Delete(g_land_cache[0]);g_land_cache[0]=fresh;landing_apply(0,fresh);
 assert(g_railSel==0&&g_railItem==2&&g_heroIdx==0);
 g_land_thread=(void*)1;g_land_queued_tab=3;load_landing(1);assert(g_land_queued_tab==-1&&!starts);
 load_landing(2);assert(g_land_queued_tab==2);load_landing(0);assert(g_land_queued_tab==-1);
 g_railSel=g_railsN;landing_capture_focus();load_landing(1);load_landing(0);assert(g_railSel==g_railsN);
 enter_tab(TAB_DOWNLOADS);assert(g_history_context_initialized&&g_dlView==0&&g_history_zone==1);
 g_dlView=3;g_list_sel=2;g_history_scroll=300;g_list_grid_scroll=500;g_history_menu=1;g_history_menu_item_id=20;
 enter_tab(1);enter_tab(TAB_DOWNLOADS);
 assert(g_dlView==3&&g_list_sel==2&&g_history_scroll==300&&g_list_grid_scroll==500&&!g_history_menu&&!g_history_menu_item_id);
 puts("PASS actual landing C: per-tab context, rebuilt catalog IDs, hero identity, discovery focus, cached latest intent cancels stale queued fetch.");
 assert(player_touch_episode(720,110,0,2)==0);assert(player_touch_episode(720,164,0,2)==-1);
 assert(player_touch_episode(719,110,0,2)==-1);assert(player_touch_episode(1240,110,0,2)==-1);
 assert(player_touch_episode(720,234,0,2)==-1);assert(player_episode_first(19,20)==12);
 episodes_menu=1;episode_count=20;episodes_sel=0;
 assert(tap(800,180)==JOY_A&&episodes_sel==1);
 assert(tap(70,55)==JOY_B);assert(tap(70,655)==-1);
 assert(touch(SDL_FINGERDOWN,800,200,1)==-1);
 assert(touch(SDL_FINGERMOTION,800,200,2)==-1);
 assert(touch(SDL_FINGERMOTION,800,80,1)==-1&&episodes_sel==4);
 assert(touch(SDL_FINGERUP,800,80,1)==-1);
 episodes_menu=0;timeline_seek=1;
 assert(touch(SDL_FINGERDOWN,48,600,1)==-1&&timeline_seek_target==0);
 assert(touch(SDL_FINGERMOTION,588,600,1)==-1&&timeline_seek_target>898&&timeline_seek_target<902);
 assert(touch(SDL_FINGERUP,588,600,1)==-1); // Preview, never seek by releasing.
 assert(tap(900,658)==JOY_A);assert(tap(1100,658)==JOY_B);assert(tap(70,55)==JOY_B);
 assert(tap(300,655)==-1);assert(tap(800,500)==-1);
 assert(touch(SDL_FINGERDOWN,900,658,1)==-1);assert(touch(SDL_FINGERUP,1100,658,1)==-1);
 timeline_seek=0;touch(SDL_FINGERUP,0,0,1);assert(!modal_touch_active);
 assert(touch(SDL_FINGERDOWN,1150,55,1)==JOY_DLEFT);
 assert(touch(SDL_FINGERDOWN,150,650,1)==JOY_L);
 assert(touch(SDL_FINGERDOWN,250,650,1)==JOY_R);
 sequential_stream=1;assert(touch(SDL_FINGERDOWN,250,650,1)==-1);
 sequential_stream=0;preview(588,0);
 assert(timeline_seek&&paused&&!timeline_seek_was_paused&&modal_touch_active&&timeline_seek_target==900);
 assert(touch(SDL_FINGERMOTION,804,600,1)==-1&&timeline_seek_target>1259&&timeline_seek_target<1261);
 assert(touch(SDL_FINGERUP,804,600,1)==-1);assert(tap(1100,658)==JOY_B);
 preview(588,1);assert(timeline_seek_was_paused&&paused);
 puts("PASS actual modal gesture C: tap rows, gaps, bounded swipe, secondary finger ignored, visible close/confirm/cancel, timeline preview requires separate confirmation, no hidden HUD action.");
 for(int i=0;i<5;i++)cJSON_Delete(g_land_cache[i]);
 return 0;
}
