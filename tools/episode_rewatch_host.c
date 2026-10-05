/* Actual main.c resume selector + next selector + playback loop. API/SDL stubs.
 * No Switch graphics/audio/network or production writes. */
#include "episode_flow.h"
#include "audio_policy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {int item_id,watched;char label[256];} PlayerEpisode;
static cJSON *g_ser,*detail;
static int g_running=1,g_series_audio_explicit,g_next_audio_pref_override,g_next_audio_pref_explicit,g_next_audio_hint,g_pref_audio,g_last_audio_index;
static char g_next_audio_language[8],g_last_audio_language[8];
static void *gRen;
static int g_playback_chain,g_seasonIdx,g_epSel,g_epScroll,g_episode_scroll_x,g_season_scroll_x,g_ep_plot_id,g_pref_autoplay=1,g_screen;
enum {SC_SERIES=1};
enum {JOY_A,JOY_B,JOY_X,JOY_PLUS,JOY_Y,JOY_ZL,JOY_ZR,JOY_DLEFT,JOY_DRIGHT,JOY_UP,JOY_DOWN,JOY_L,JOY_R,JOY_MINUS};
#define RCW 164
#define RGAP 14
#define WIN_W 1280
static int g_dlmenu,g_ep_plot_scroll,g_ep_plot_count;
static PlayerEpisode *g_play_episodes;
static int g_play_episode_count,g_play_episode_current,g_play_chosen_item;
static int ids[8],plays,mode,prompt_accept=1,prompts,requests;
static int jint(const cJSON *j,const char *key) {const cJSON*v=cJSON_GetObjectItem(j,key);return cJSON_IsNumber(v)?v->valueint:0;}
static const char*jstr(const cJSON*j,const char*key) {const cJSON*v=cJSON_GetObjectItem(j,key);return cJSON_IsString(v)?v->valuestring:NULL;}
static int arr_len(const cJSON*j) {return cJSON_GetArraySize(j);}
static cJSON *ser_obj(void) {return cJSON_GetObjectItem(g_ser,"series");}
static cJSON *ser_audio(void) {return cJSON_GetObjectItem(ser_obj(),"audio_versions");}
static cJSON *seasons_obj(void) {return cJSON_GetObjectItem(g_ser,"seasons");}
static int ser_grouped(void) {return arr_len(cJSON_GetObjectItem(ser_obj(),"season_group"))>1;}
static cJSON *ser_ep_at(int i) {
    if(!ser_grouped()) return cJSON_GetArrayItem(cJSON_GetArrayItem(seasons_obj(),g_seasonIdx),i);
    cJSON *season; cJSON_ArrayForEach(season,seasons_obj()) {int n=arr_len(season);if(i<n)return cJSON_GetArrayItem(season,i);i-=n;}return NULL;
}
static const char*ep_display_title(cJSON*e) {return jstr(e,"title");}
static void toast(const char*s) {(void)s;}
static void diag_player_event(const char *area,const char *event,const char *fmt,...) {(void)area;(void)event;(void)fmt;}
static void fetch_episode_context(int sid,int finished,int first,int explicit_next) {(void)sid;(void)finished;(void)first;(void)explicit_next;assert(!"unexpected async fetch");}
static int prompt_next_episode(cJSON*ep,cJSON*series) {(void)series;prompts++;assert(jint(ep,"id")==302);return prompt_accept;}
static cJSON *ui_request_get(void*r,const char*path,int*running,int*cancelled) {(void)r;(void)running;assert(!strcmp(path,"/api/catalog/series/10"));*cancelled=0;requests++;return cJSON_Duplicate(detail,1);}
static void rebuild_series_plot(void) {}
static int ser_nep(void) {return arr_len(cJSON_GetArrayItem(seasons_obj(),g_seasonIdx));}
static int season_count(void) {return arr_len(seasons_obj());}
static int ser_nseasons(void) {return season_count();}
static cJSON *ser_group(void) {return cJSON_GetObjectItem(ser_obj(),"season_group");}
static int ser_group_idx(void) {return 0;}
static void input_dlmenu(int b) {(void)b;assert(!"unexpected download menu");}
static void detail_return_to_origin(void) {assert(!"unexpected back");}
static void toggle_fav_series(int id) {(void)id;assert(!"unexpected favorite");}
static void media_list_prompt_add(int id,int kind,const char *title,const char *logo) {(void)id;(void)kind;(void)title;(void)logo;assert(!"unexpected collection");}
static void open_dlmenu(void) {assert(!"unexpected download menu");}
static void open_series_mode(int id,int explicit_audio) {(void)id;(void)explicit_audio;assert(!"unexpected grouped season");}
static void series_keep_audio_begin(void) {}
static void reveal_horizontal_item(int *scroll,int sel,int count,int width,int gap,int viewport) {(void)scroll;(void)sel;(void)count;(void)width;(void)gap;(void)viewport;}
static void playback_memory_leave(void) {}
static int resolve_and_play_details(int id,const char*t,const char*sub,const char*overview,const char*next,int has_next) {
    (void)t;(void)sub;(void)overview;(void)next;(void)has_next;
    assert(plays<8);ids[plays++]=id;
    if(mode==1 && plays==1){g_play_chosen_item=301;return 2;}
    if((mode==1&&plays==2)||(mode!=1&&plays==1)) return mode==2?2:1;
    return 0;
}
#include "audit_rewatch_actual.inc"
static void reset(void) {if(g_ser)cJSON_Delete(g_ser);g_ser=cJSON_Duplicate(detail,1);plays=prompts=requests=0;mode=0;g_pref_autoplay=1;prompt_accept=1;g_playback_chain=0;select_series_resume_target(g_ser);}
int main(void) {
    // Season 5 has the most recent *unfinished* progress and is inserted first.
    // Season 3 was already watched, but user is intentionally replaying it.
    detail=cJSON_Parse("{\"series\":{\"id\":10,\"title\":\"Serie\"},\"seasons\":{\"5\":[{\"id\":507,\"season\":5,\"episode\":7,\"title\":\"T5E7\",\"position_seconds\":600,\"completed\":0,\"progress_updated_at\":\"2026-10-05 01:00:00\"}],\"3\":[{\"id\":301,\"season\":3,\"episode\":1,\"title\":\"T3E1\",\"position_seconds\":1300,\"completed\":1},{\"id\":302,\"season\":3,\"episode\":2,\"title\":\"T3E2\",\"position_seconds\":1300,\"completed\":true},{\"id\":303,\"season\":3,\"episode\":3,\"title\":\"T3E3\",\"completed\":1}]}}");assert(detail);
    reset();assert(jint(ser_ep_at(g_epSel),"id")==507);
    play_episode_sequence(301,10,"Serie",NULL);
    assert(plays==2&&ids[0]==301&&ids[1]==302&&prompts==1);
    puts("PASS actual selectors: old T5E7 resume must not override natural T3E1 -> T3E2 (already watched).");
    reset();mode=1;play_episode_sequence(507,10,"Serie",NULL);
    assert(plays==3&&ids[0]==507&&ids[1]==301&&ids[2]==302);
    puts("PASS actual loop: episode-panel T5E7 -> T3E1 -> natural T3E2.");
    reset();mode=2;play_episode_sequence(301,10,"Serie",NULL);
    assert(plays==2&&ids[1]==302&&prompts==0);
    puts("PASS explicit Next: T3E1 -> T3E2, not latest progress T5E7.");
    reset();g_pref_autoplay=0;play_episode_sequence(301,10,"Serie",NULL);
    assert(plays==1&&jint(ser_ep_at(g_epSel),"id")==302);
    puts("PASS autoplay off: select T3E2 but do not open it.");
    reset();prompt_accept=0;play_episode_sequence(301,10,"Serie",NULL);
    assert(plays==1&&jint(ser_ep_at(g_epSel),"id")==302);
    puts("PASS cancelled countdown: remains on T3E2, not T5E7.");
    reset();cJSON_Delete(g_ser);g_ser=NULL;play_episode_sequence(301,10,"Serie",NULL);
    assert(requests==1&&plays==2&&ids[1]==302);
    puts("PASS direct/history context fetch: resume preselects T5E7 but current T3E1 advances to T3E2.");
    reset();assert(jint(ser_ep_at(g_epSel),"id")==507);
    input_series(JOY_R);assert(jint(ser_ep_at(g_epSel),"id")==301);
    input_series(JOY_A);assert(plays==2&&ids[0]==301&&ids[1]==302);
    puts("PASS actual series list: R from selected season 5 to season 3; A plays T3E1 -> T3E2.");
    reset();input_series(JOY_R);input_series(JOY_DRIGHT);
    assert(jint(ser_ep_at(g_epSel),"id")==302);mode=3;
    // One playback only: test that the list's explicit episode is honored.
    g_pref_autoplay=0;input_series(JOY_A);assert(plays==1&&ids[0]==302);
    puts("PASS actual series list: selecting already-watched T3E2 never opens latest T5E7.");
    reset();cJSON_Delete(g_ser);g_ser=NULL;cJSON_Delete(detail);detail=NULL;
    puts("REWATCH AUDIT: 8 scenarios passed; no physical Switch or real API used.");
}
