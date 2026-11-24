#include <string.h>
#include "audit062_identity_support.inc"
enum {SC_LOGIN=1,SC_MAIN=2,JOY_DLEFT=3,JOY_DRIGHT,JOY_B,JOY_MINUS,JOY_PLUS,JOY_A,FETCH_PROFILES};
static char g_token[640]="old-token-fixture",g_user[128]="old-user-fixture",g_status[256];
static int g_profile_id=1,g_profile_required,g_screen=SC_MAIN,g_running=1;
static int g_profiles_return=SC_MAIN,g_profile_sel,g_pref_seek_stick,net_profile=1,restarts;
static Uint32 g_restart_at;
static cJSON*g_profiles;
static void store_clear_token(void){}
static void store_clear_user(void){}
static void store_clear_profile_id(void){}
static void net_set_profile_id(int n){net_profile=n;}
static int store_save_profile_id(int n){return n>0;}
static void store_select_profile(int n,const char*user){assert(user&&user[0]);store_profile=n;}
static int store_load_seek_stick(void){return 0;}
static void load_favs(void){}
static void enter_tab(int n){assert(n==0);}
static void begin_catalog_fetch(int n,const char*path,void*p){(void)n;(void)path;(void)p;}
static void logout_and_restart(void){restarts++;}
static int schedule_restart(void*p){(void)p;restarts++;return 0;}
static Uint32 SDL_GetTicks(void){return 100;}
static const char*jstr(cJSON*j,const char*k){const cJSON*v=cJSON_GetObjectItem(j,k);return cJSON_IsString(v)?v->valuestring:NULL;}
#include "audit062_session.inc"
#include "audit062_profile.inc"
static cJSON*g_account_pending,*g_account_status,*g_settings_accel_pending,*g_accel_status;
static SDL_atomic_t g_account_ready,g_settings_done;
static void*g_settings_thread;
static unsigned g_settings_prefs_revision,g_prefs_revision;
static int g_account_prefs_loaded,g_pref_hide_adult=1,g_pref_autoplay=1,g_pref_reduce_motion,g_pref_audio,g_settings_refresh_pending;
static int settings_requests;
static void load_settings_status(void){settings_requests++;}
static void landing_prefs_gate(void){
#include "audit062_prefs_gate.inc"
}
#include "audit062_settings.inc"
int main(void){
 g_history=cJSON_Parse("{\"items\":[{\"item_id\":11}]}");
 g_history_pending=cJSON_Parse("{\"items\":[{\"item_id\":99}]}");
 g_watchlater_pending=cJSON_Parse("{\"items\":[{\"item_id\":123,\"title\":\"old fixture\"}]}");
 g_account_pending=cJSON_Parse("{\"prefs\":{\"audioPref\":\"leg\",\"hideAdult\":false}}");
 g_account_ready.value=1;
 g_account_prefs_loaded=1; // landing marks the old settings request loaded before its completion
 // Completed responses from user/profile1 exist when a catalog401 is accepted.
 session_invalid_to_login("fixture401");assert(!g_token[0]&&g_profile_id==0&&net_profile==0&&g_screen==SC_LOGIN);
 // Reauthentication in the same process, as QR/password flow does.
 strcpy(g_token,"new-token-fixture");strcpy(g_user,"new-user-fixture");
 g_profile_required=1;g_profiles=cJSON_Parse("{\"profiles\":[{\"id\":2}]}");
 input_profiles(JOY_A);assert(g_profile_id==2&&store_profile==2&&net_profile==2&&!restarts);
 landing_prefs_gate();assert(g_account_prefs_loaded&&!settings_requests);
 assert(jint(cJSON_GetArrayItem(history_items(),0),"item_id")==11);
 pump_history();
 assert(jint(cJSON_GetArrayItem(history_items(),0),"item_id")==99);
 assert(reconciles==1&&reconciled_profile==2);
 assert(!g_pref_audio);pump_settings_status();assert(g_pref_audio==1&&!g_pref_hide_adult);
 printf("REPRODUCED reauth identity gap: old history retained/applied; old watchlater response dispatched to profile%d; old account prefs set audio to Legendado after new login. HTTP/storage fake, real clear/profile/history/settings C.\n",reconciled_profile);
 puts("REPRODUCED preference refresh gate: old loaded flag survives reauth and suppresses the new account's automatic preference fetch.");
 // Normal profile switch is protected: restart, no in-process store mutation.
 cJSON_Delete(g_profiles);g_profiles=cJSON_Parse("{\"profiles\":[{\"id\":3}]}");
 input_profiles(JOY_A);assert(restarts==1&&store_profile==2&&g_profile_id==2&&net_profile==2);
 puts("HEALTHY: normal switch from profile2 to3 schedules restart, not the vulnerable reauthentication path.");
 cJSON_Delete(g_profiles);cJSON_Delete(g_history);cJSON_Delete(g_account_status);
 return 0;
}
