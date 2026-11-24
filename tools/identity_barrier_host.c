#define _POSIX_C_SOURCE 200809L
#include "catalog_fetch.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
enum {SC_LOGIN,SC_MAIN,LOGIN_PAIR_IDLE};
#define TAB_SAGAS 4
struct SDL_Thread {pthread_t id;SDL_atomic_t*done,*cancel;cJSON**mailbox;int held,finished;};
static SDL_atomic_t release_worker,started;
static int joined,created,net_profile=7;
int SDL_AtomicGet(SDL_atomic_t*a){return __atomic_load_n(&a->value,__ATOMIC_SEQ_CST);}
int SDL_AtomicSet(SDL_atomic_t*a,int n){return __atomic_exchange_n(&a->value,n,__ATOMIC_SEQ_CST);}
static void nap(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static char g_token[640]="old-fixture",g_user[128]="old-user";
static void*worker(void*arg){SDL_Thread*t=arg;assert(net_profile==7&&!strcmp(g_token,"old-fixture"));__atomic_add_fetch(&started.value,1,__ATOMIC_SEQ_CST);
 while(!SDL_AtomicGet(&release_worker)&&(t->held||!SDL_AtomicGet(t->cancel)))nap();
 assert(net_profile==7&&!strcmp(g_token,"old-fixture"));
 *t->mailbox=cJSON_Parse("{\"old\":true}");assert(*t->mailbox);t->finished=1;SDL_AtomicSet(t->done,1);return NULL;}
static SDL_Thread*launch(SDL_atomic_t*done,SDL_atomic_t*cancel,cJSON**mailbox,int held){
 SDL_Thread*t=calloc(1,sizeof(*t));assert(t);t->done=done;t->cancel=cancel;t->mailbox=mailbox;t->held=held;
 SDL_AtomicSet(done,0);assert(!pthread_create(&t->id,NULL,worker,t));created++;return t;}
void SDL_WaitThread(SDL_Thread*t,int*s){(void)s;assert(SDL_AtomicGet(t->done)&&t->finished);assert(!pthread_join(t->id,NULL));free(t);joined++;}
void catalog_fetch_cancel(CatalogFetch*f){if(f->thread)SDL_AtomicSet(&f->cancel,1);}
void catalog_fetch_dispose(CatalogFetch*f){if(f->thread){assert(SDL_AtomicGet(&f->done));SDL_WaitThread(f->thread,NULL);f->thread=NULL;}cJSON_Delete(f->result);f->result=NULL;memset(f->bearer,0,sizeof(f->bearer));}
static SDL_Thread *g_land_thread,*g_history_thread,*g_watchlater_thread,*g_settings_thread,*g_avatar_thread,*g_dl_thread;
static SDL_atomic_t g_land_done,g_history_done,g_watchlater_done,g_settings_done,g_avatar_done,g_dl_done,g_identity_cancel;
static struct {SDL_Thread*thread;SDL_atomic_t done,cancel;pthread_mutex_t*mutex;int stage,result,restart_requested;char token[640];}g_pair;
static CatalogFetch g_fetch,g_favs_fetch,g_dl_done_fetch;
static int g_identity_reset_pending,g_profile_menu,g_history_menu,g_avatar_picker,g_profile_editor;
static cJSON *g_land,*g_heroesArr,*g_land_cache[5],*g_land_pending,*g_history_pending,*g_watchlater_pending,*g_account_pending,*g_settings_accel_pending,*g_avatar_pending,*g_dl_pending;
static cJSON *g_history,*g_search,*g_ser,*g_saga_detail,*g_profiles,*g_avatar_catalog,*g_account_status,*g_accel_status,*g_dl;
static int g_railsN,g_rails[48],g_landing_focus[5],g_search_window,g_search_counts_valid,g_avatar_item_n;
static cJSON*g_avatar_items[256];static int g_avatar_lookup[512];
typedef struct {int kind;}FetchIntent;
static FetchIntent g_fetch_current,g_fetch_queued;
static int g_fetch_discard,g_land_fetch_tab,g_land_queued_tab,g_land_applied_tab;
static int g_account_prefs_loaded,g_settings_refresh_pending,g_history_refresh_requested;
static int g_pref_audio,g_pref_autoplay,g_pref_hide_adult,g_pref_reduce_motion;
static unsigned g_prefs_revision,g_watchlater_revision,g_watchlater_fetch_revision;
static int g_next_audio_hint,g_last_audio_index,g_next_audio_pref_override,g_next_audio_pref_explicit,g_favItemN,g_favSeriesN,g_favs_profile_id;
static char g_next_audio_language[8],g_last_audio_language[8],g_srchQuery[128];
static struct {int active;}g_episode_pending;
static int g_dlDoneN,g_dl_done_requested,g_dl_done_inflight,g_dl_done_profile,g_dlgN,g_dlView,g_history_sel,g_history_context_initialized;
static int g_avatar_due,g_avatar_attempted,g_avatar_failures,g_avatar_picker_await,g_srchSel,g_srchScroll,g_srchFilter;
static int g_profile_id=7,g_profile_required,g_screen,cleared,closed_movies,closed_episodes;
static void close_movie_details(void){closed_movies++;}
static void play_episodes_clear(void){closed_episodes++;}
static void net_set_profile_id(int n){net_profile=n;}
static void SDL_LockMutex(pthread_mutex_t*m){assert(!pthread_mutex_lock(m));}
static void SDL_UnlockMutex(pthread_mutex_t*m){assert(!pthread_mutex_unlock(m));}
static void store_clear_token(void){cleared++;}
static void store_clear_user(void){}
static void store_clear_profile_id(void){}
static void toast(const char*s){assert(s);}
static void identity_reset_begin(void);
#include "identity_session.inc"
#include "identity_barrier.inc"
int main(void){
 pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;g_pair.mutex=&mutex;
 for(int epoch=0;epoch<100;epoch++){
  int previous_joined=joined;SDL_AtomicSet(&release_worker,0);SDL_AtomicSet(&started,0);
  net_profile=g_profile_id=7;strcpy(g_token,"old-fixture");strcpy(g_user,"old-user");
  g_account_prefs_loaded=1;g_pref_audio=1;g_pref_hide_adult=0;g_pref_reduce_motion=1;
  g_history=cJSON_Parse("{\"items\":[1]}");g_account_status=cJSON_Parse("{\"old\":1}");
  for(int i=0;i<5;i++)g_land_cache[i]=cJSON_Parse("{\"old\":true}");
  g_land=g_land_cache[0];
  g_history_thread=launch(&g_history_done,&g_identity_cancel,&g_history_pending,1);
  g_watchlater_thread=launch(&g_watchlater_done,&g_identity_cancel,&g_watchlater_pending,0);
  g_land_thread=launch(&g_land_done,&g_identity_cancel,&g_land_pending,0);
  g_settings_thread=launch(&g_settings_done,&g_identity_cancel,&g_account_pending,0);
  g_avatar_thread=launch(&g_avatar_done,&g_identity_cancel,&g_avatar_pending,0);
  g_dl_thread=launch(&g_dl_done,&g_identity_cancel,&g_dl_pending,0);
  SDL_AtomicSet(&g_pair.cancel,0);g_pair.thread=launch(&g_pair.done,&g_pair.cancel,&g_settings_accel_pending,0);
  CatalogFetch*jobs[]={&g_fetch,&g_favs_fetch,&g_dl_done_fetch};
  for(int i=0;i<3;i++){SDL_AtomicSet(&jobs[i]->cancel,0);jobs[i]->thread=launch(&jobs[i]->done,&jobs[i]->cancel,&jobs[i]->result,0);}
  while(SDL_AtomicGet(&started)!=10)nap();
  session_invalid_to_login("fixture401");assert(g_screen==SC_LOGIN&&g_identity_reset_pending);
  for(int i=0;i<10;i++){identity_reset_pump();assert(g_identity_reset_pending&&g_history&&!strcmp(g_token,"old-fixture")&&net_profile==7);nap();}
  assert(joined-previous_joined<=9);SDL_AtomicSet(&release_worker,1);
  for(int i=0;i<2000&&g_identity_reset_pending;i++){identity_reset_pump();nap();}
  assert(!g_identity_reset_pending&&joined-previous_joined==10&&!g_token[0]&&!g_user[0]&&!net_profile);
  assert(!g_history&&!g_history_pending&&!g_watchlater_pending&&!g_account_pending&&!g_account_status&&!g_land);
  assert(!g_account_prefs_loaded&&!g_pref_audio&&g_pref_hide_adult&&!g_pref_reduce_motion);
  for(int i=0;i<5;i++)assert(!g_land_cache[i]);
 }
 assert(created==joined&&created==1000&&cleared==100&&closed_movies==100&&closed_episodes==100);
 pthread_mutex_destroy(&mutex);
 puts("PASS actual identity barrier: 100 reauthentication epochs /1000 real pthread joins, delayed cancellation, immutable old token/profile until ALL joins, old results discarded, caches/prefs cleared, no renderer join before done. HTTP/SD/render mocked.");return 0;
}
