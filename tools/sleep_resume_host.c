#include "api.h"
#include "net.h"
#include "diag.h"
#include "player_lifecycle.h"
#include <switch.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const char *BASE="https://nplay.test";
char g_token[640]="fixture-token";
static int refresh_code=200,admission_code=200,refresh_calls,resolve_calls,bad_source,cancel_after_refresh;
static AppletHookFn hook;static void *hook_data;static int hooks,unhooks;
static AppletFocusState focus=AppletFocusState_InFocus;
void appletHook(AppletHookCookie *c,AppletHookFn f,void *p){(void)c;assert(!hook);hook=f;hook_data=p;hooks++;}
void appletUnhook(AppletHookCookie *c){(void)c;assert(hook);hook=NULL;unhooks++;}
AppletFocusState appletGetFocusState(void){return focus;}
Uint32 SDL_GetTicks(void){return 100;}
int SDL_AtomicGet(SDL_atomic_t*v){return __atomic_load_n(&v->value,__ATOMIC_SEQ_CST);}
int SDL_AtomicSet(SDL_atomic_t*v,int n){return __atomic_exchange_n(&v->value,n,__ATOMIC_SEQ_CST);}
void diag_network_event(const char*m,const char*p,long code,unsigned ms,size_t bytes){
 (void)code;(void)ms;(void)bytes;assert(!strcmp(m,"POST"));
 assert(!strcmp(p,"/api/stream/session/77/refresh")||!strcmp(p,"/api/stream/42"));
}
long net_request_timeout_cancel(const char*url,const char*method,const char*body,
 const char*bearer,struct membuf*out,const char**err,long connect,long total,SDL_atomic_t*cancel){
 assert(!strcmp(method,"POST")&&!strcmp(bearer,"fixture-token"));
 assert(!cancel||!SDL_AtomicGet(cancel));assert(connect==4&&total==8);
 assert(out->limit==64u*1024u);*err=NULL;long code;const char *response;
 if(!strcmp(url,"https://nplay.test/api/stream/session/77/refresh")){
  refresh_calls++;assert(!strcmp(body,"{\"source_id\":7}"));code=refresh_code;
  response=code==200?"{\"source_id\":7,\"play_url\":\"/api/play/42?token=renewed\"}":
   code==401?"{\"error\":\"Acesso expirado\",\"reason\":\"expired\"}":"{\"error\":\"Sessao encerrada\"}";
  if(cancel_after_refresh&&cancel)SDL_AtomicSet(cancel,1);
 }else{
  assert(!strcmp(url,"https://nplay.test/api/stream/42"));resolve_calls++;
  assert(!strcmp(body,"{\"quality\":\"1080p\"}"));code=admission_code;
  response=code==401?"{\"error\":\"Acesso expirado\",\"reason\":\"expired\"}":code!=200?"{\"error\":\"Indisponivel\"}":
   bad_source==1?"{\"container\":\"torrent\",\"play_url\":\"magnet:?fixture\"}":
   bad_source==2?"{\"delivery\":\"r2\",\"play_url\":\"/fixture\"}":
   "{\"delivery\":\"r2\",\"container\":\"m3u8\",\"session_id\":78,\"source_id\":7,\"season\":3,\"episode\":2,\"play_url\":\"/api/play/42?token=fresh\"}";
 }
 out->len=strlen(response);out->data=malloc(out->len+1);assert(out->data);
 memcpy(out->data,response,out->len+1);return code;
}
long net_request_timeout(const char*u,const char*m,const char*b,const char*t,struct membuf*o,const char**e,long c,long a){return net_request_timeout_cancel(u,m,b,t,o,e,c,a,NULL);}
void membuf_free(struct membuf*m){free(m->data);m->data=NULL;}
#include "sleep_resume_callback.inc"
int main(void){
 for(int i=0;i<1000;i++){
  player_lifecycle_begin();assert(!player_lifecycle_pending());
  focus=AppletFocusState_OutOfFocus;hook(AppletHookType_OnFocusState,hook_data);
  focus=AppletFocusState_InFocus;hook(AppletHookType_OnFocusState,hook_data);assert(!player_lifecycle_pending());
  focus=AppletFocusState_Background;hook(AppletHookType_OnFocusState,hook_data);
  focus=AppletFocusState_InFocus;hook(AppletHookType_OnFocusState,hook_data);assert(player_lifecycle_pending());
  hook(AppletHookType_OnResume,hook_data);assert(player_lifecycle_pending());
  player_lifecycle_acknowledge();assert(!player_lifecycle_pending());
  hook(AppletHookType_OnResume,hook_data);assert(player_lifecycle_pending());
  player_lifecycle_end();assert(!player_lifecycle_pending());
 }
 assert(hooks==1000&&unhooks==1000&&!hook);
 PlaybackSource current={.item_id=42,.session_id=77,.source_id=7,.delivery=DELIVERY_R2};
 snprintf(current.delivery_str,sizeof(current.delivery_str),"r2");snprintf(current.container,sizeof(current.container),"m3u8");
 snprintf(current.quality,sizeof(current.quality),"1080p");snprintf(current.play_url,sizeof(current.play_url),"https://nplay.test/api/play/42?token=old");
 PlaybackSource output={0};SDL_atomic_t cancel={0};
 assert(!on_player_renew(&current,&output,&cancel,NULL)&&output.session_id==77&&refresh_calls==1&&!resolve_calls);
 refresh_code=404;refresh_calls=resolve_calls=0;
 assert(!on_player_renew(&current,&output,&cancel,NULL)&&output.session_id==78&&refresh_calls==1&&resolve_calls==1);
 assert(output.item_id==42&&output.season==3&&output.episode==2);
 int denied[]={401,402,403,409,429,503,-28,-60,-77};
 for(size_t i=0;i<sizeof(denied)/sizeof(*denied);i++){
  refresh_code=denied[i];refresh_calls=resolve_calls=0;PlaybackSource old=output;
  assert(on_player_renew(&current,&output,&cancel,NULL)==-1&&!resolve_calls&&refresh_calls==1);
  assert(!memcmp(&old,&output,sizeof(old)));assert(api_last_error_http_code()==denied[i]);
  assert(api_last_error_access_expired()==(denied[i]==401));
 }
 refresh_code=404;
 for(size_t i=0;i<sizeof(denied)/sizeof(*denied);i++){
  admission_code=denied[i];refresh_calls=resolve_calls=0;
  assert(on_player_renew(&current,&output,&cancel,NULL)==-3&&resolve_calls==1&&refresh_calls==1);
 }
 admission_code=200;
 for(bad_source=1;bad_source<=2;bad_source++){PlaybackSource old=output;
  assert(on_player_renew(&current,&output,&cancel,NULL)==-3&&!memcmp(&old,&output,sizeof(old)));
 }
 bad_source=0;cancel_after_refresh=1;resolve_calls=0;
 assert(on_player_renew(&current,&output,&cancel,NULL)==-1&&!resolve_calls);
 refresh_calls=resolve_calls=0;assert(on_player_renew(&current,&output,&cancel,NULL)==-1&&!refresh_calls&&!resolve_calls);
 puts("PASS actual lifecycle/API/callback: 1000 HOME/resume/keyboard cycles; closed-session readmission once, quality/context, auth/plan/device/screens/server/TLS/cancel denials, no magnet/partial descriptor, old output retained.");
}
