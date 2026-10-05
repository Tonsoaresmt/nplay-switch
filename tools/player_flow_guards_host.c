#include "cJSON.h"
#include "player_completion.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
typedef uint32_t Uint32;
typedef struct {int value;} SDL_atomic_t;
static int SDL_AtomicGet(SDL_atomic_t*v){return v->value;}
static void SDL_WaitThread(void*t,void*s){(void)t;(void)s;}
static int jint(const cJSON*j,const char*k){const cJSON*v=cJSON_GetObjectItem(j,k);return cJSON_IsNumber(v)?v->valueint:0;}
static int arr_len(const cJSON*j){return cJSON_GetArraySize(j);}
static cJSON *g_history,*g_history_pending,*g_watchlater_pending;
static void *g_history_thread,*g_watchlater_thread;
static SDL_atomic_t g_history_done,g_watchlater_done;
static int g_history_sel,g_history_zone,g_history_refresh_requested,g_history_menu,g_history_menu_item_id;
static Uint32 g_watchlater_revision,g_watchlater_fetch_revision;
static int g_open_list=-1,g_list_item_sel,reconciles,refreshes;
static int notices;
static void toast(const char*s){(void)s;notices++;}
static cJSON *history_items(void);
static void load_history(void){refreshes++;}
static int store_media_list_create(const char*s){(void)s;return 0;}
static int store_media_list_get(int l,int n,int*id,int*k,char*t,size_t tc,char*u,size_t uc){(void)l;(void)n;(void)id;(void)k;(void)t;(void)tc;(void)u;(void)uc;return 0;}
static int store_media_list_item_count(int l){(void)l;return 0;}
static int store_watchlater_reconcile(int l,const cJSON*a){(void)l;assert(cJSON_IsArray(a));reconciles++;return 0;}
#include "flow_guards_history.inc"
enum {EXIT_REASON_NATURAL,EXIT_REASON_USER};
typedef struct {int reason,presented_frame;double position,duration;} PlayerResult;
typedef struct {int item_id,saved,completed;} PlaybackSyncStatus;
static int marked;
static int api_mark_watched(int id){(void)id;marked++;return 0;}
#include "flow_guards_finalize.inc"
#define AVERROR_EOF (-541478725)
typedef struct {int stream_index;} AVPacket;
static void av_packet_unref(AVPacket*p){(void)p;}
static int SDL_GetQueuedAudioSize(int device){(void)device;return 0;}
static void SDL_Delay(int ms){(void)ms;}
static void player_error_text(const char*s,int error){(void)s;(void)error;}
static void player_error_message(const char*s){(void)s;}
static void diag_player_event(const char*a,const char*b,const char*c,...){(void)a;(void)b;(void)c;}
static int drains;
static int terminal(int input,double cur_pos,double dur,int io_failed){
    struct {int cancelled;SDL_atomic_t hls_io_failed;} open_watch={0};
    open_watch.hls_io_failed.value=io_failed;
    int running=1,adev=0,native_hls=1,logged_first_present=1,reached_end=0,playback_error=0;
    int eof_drain_stage=0,vidx=0,aidx=1;AVPacket packet={0},*pkt=&packet;
    drains=0;
    while(running){int ret=input,drain_decoder=0;
#include "flow_guards_terminal.inc"
        if(drain_decoder){assert(pkt->stream_index==drains);drains++;}
    }
    return playback_error?playback_error:reached_end;
}
static void pending(const char*s){g_history_pending=cJSON_Parse(s);assert(g_history_pending);pump_history();}
int main(void){
    g_history=cJSON_Parse("{\"items\":[{\"item_id\":10},{\"item_id\":20}]}");assert(g_history);
    g_history_sel=1;g_history_menu=1;g_history_menu_item_id=20;
    pending("{\"items\":[{\"item_id\":30},{\"item_id\":10},{\"item_id\":20}]}");
    assert(g_history_menu&&g_history_sel==2&&jint(cJSON_GetArrayItem(history_items(),g_history_sel),"item_id")==20);
    assert(history_index_for_id(history_items(),20)==2);
    cJSON *known=g_history;int prior_notices=notices;
    pending("{\"error\":\"wrong schema\"}");assert(g_history==known&&g_history_sel==2&&notices==prior_notices+1);
    pending("{\"items\":[{\"item_id\":10}]}");assert(!g_history_menu&&!g_history_menu_item_id&&g_history_sel==0);
    g_history_menu=0;g_history_sel=0;
    pending("{\"items\":[{\"item_id\":30},{\"item_id\":10}]}");assert(g_history_sel==1);
    pending("{\"items\":[]}");assert(arr_len(history_items())==0&&g_history_zone==1);
    assert(history_index_for_id(history_items(),0)==-1&&history_index_for_id(history_items(),10)==-1);
    puts("PASS history: menu/selection by identity, removal closes menu, invalid schema preserved, valid empty accepted.");
    g_watchlater_pending=cJSON_Parse("{\"items\":[]}");pump_history();assert(reconciles==1);
    g_watchlater_pending=cJSON_Parse("{\"error\":1}");pump_history();assert(reconciles==1);
    g_watchlater_revision=1;g_watchlater_pending=cJSON_Parse("{\"items\":[]}");pump_history();
    assert(reconciles==1&&refreshes==1&&!g_watchlater_pending);
    puts("PASS watchlater dispatch: schema guard, empty reconciliation, stale mutation snapshot discarded/refetched.");
    assert(terminal(AVERROR_EOF,300,1800,0)==-5&&drains==2);
    assert(terminal(AVERROR_EOF,1799,1800,0)==1&&drains==2);
    assert(terminal(AVERROR_EOF,1799,1800,1)==-5);
    assert(terminal(AVERROR_EOF,300,0,1)==-5);
    assert(terminal(AVERROR_EOF,300,0,0)==1);
    assert(terminal(-5,1799,1800,0)==-5&&drains==0);
    assert(!player_eof_complete(NAN,1800,0)&&!player_eof_complete(-1,1800,0));
    assert(player_eof_complete(9,10,0)&&!player_eof_complete(5,10,0));
    PlayerResult result={EXIT_REASON_NATURAL,1,300,1800};PlaybackSyncStatus sync={0};
    assert(!finalize_natural_playback(99,&result,&sync)&&marked==0);
    result.position=1799;assert(finalize_natural_playback(99,&result,&sync)&&marked==1);
    result.reason=EXIT_REASON_USER;assert(!finalize_natural_playback(99,&result,&sync)&&marked==1);
    puts("PASS EOF: simulated drain routing, premature VOD and failed I/O rejected, normal/unknown clean EOF and user exit distinguished.");
    assert(!player_pause_holds_decode(1,0,0));
    assert(!player_pause_holds_decode(1,1,1));
    assert(player_pause_holds_decode(1,1,0));
    assert(!player_pause_holds_decode(0,1,0));
    puts("PASS paused reopen gate: decode first/new-generation frame once; hold thereafter.");
    cJSON_Delete(g_history);return 0;
}
