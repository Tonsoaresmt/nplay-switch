#include "api.h"
#include "net.h"
#include "diag.h"
#include "player_recovery.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const char *BASE="https://nplay.test";
char g_token[640]="fixture-token";
static int closed,expired,refresh_calls,resolve_calls;
Uint32 SDL_GetTicks(void){return 100;}
int SDL_AtomicGet(SDL_atomic_t*v){return v->value;}
void diag_network_event(const char*m,const char*p,long code,unsigned ms,size_t bytes){
 (void)code;(void)ms;(void)bytes;assert(!strcmp(m,"POST"));
 assert(!strcmp(p,"/api/stream/session/77/refresh")||!strcmp(p,"/api/stream/42"));
}
long net_request_timeout_cancel(const char*url,const char*method,const char*body,
 const char*bearer,struct membuf*out,const char**err,long connect,long total,SDL_atomic_t*cancel){
 assert(!strcmp(method,"POST") && !strcmp(bearer,"fixture-token"));
 assert(!cancel||!SDL_AtomicGet(cancel));assert(connect==4 && total==8);
 const char *response;long code;
 *err=NULL;
 if(!strcmp(url,"https://nplay.test/api/stream/session/77/refresh")){
  refresh_calls++;assert(!strcmp(body,"{\"source_id\":7}"));
  code=expired?401:closed?404:200;
  response=expired?"{\"error\":\"Acesso expirado\",\"reason\":\"expired\"}":
    closed?"{\"error\":\"Sessao encerrada\"}":
    "{\"source_id\":7,\"play_url\":\"/api/play/42?token=renewed\"}";
 }else{
  assert(!strcmp(url,"https://nplay.test/api/stream/42"));resolve_calls++;
  code=expired?401:200;
  response=expired?"{\"error\":\"Acesso expirado\",\"reason\":\"expired\"}":
   "{\"delivery\":\"r2\",\"container\":\"m3u8\",\"session_id\":78,\"source_id\":7,\"play_url\":\"/api/play/42?token=fresh\"}";
 }
 out->len=strlen(response);out->data=malloc(out->len+1);assert(out->data);
 memcpy(out->data,response,out->len+1);return code;
}
long net_request_timeout(const char*u,const char*m,const char*b,const char*t,struct membuf*o,const char**e,long c,long a){
 return net_request_timeout_cancel(u,m,b,t,o,e,c,a,NULL);
}
void membuf_free(struct membuf*m){free(m->data);m->data=NULL;}
#include "sleep_resume_callback.inc"
int main(void){
 PlaybackSource current={.item_id=42,.session_id=77,.source_id=7,.delivery=DELIVERY_R2};
 snprintf(current.delivery_str,sizeof(current.delivery_str),"r2");
 snprintf(current.container,sizeof(current.container),"m3u8");
 snprintf(current.play_url,sizeof(current.play_url),"https://nplay.test/api/play/42?token=old");
 PlaybackSource output={0};SDL_atomic_t cancel={0};
 assert(!on_player_renew(&current,&output,&cancel,NULL) && output.session_id==77);
 closed=1;refresh_calls=resolve_calls=0;
 for(int i=0;i<4;i++)assert(on_player_renew(&current,&output,&cancel,NULL)==-1);
 assert(refresh_calls==4 && resolve_calls==0 && !api_last_error_access_expired());
 printf("REPRODUCED: closed R2 session -> %d refreshes of the SAME session; no fresh resolution.\n",refresh_calls);
 assert(!api_reresolve_playback_cancel(current.item_id,NULL,&cancel,&output));
 assert(output.session_id==78 && output.delivery==DELIVERY_R2 && resolve_calls==1);
 assert(player_recovery_position(3000,0,0,0)==3000);
 puts("CONTROL: reselecting the episode resolves session 78; checkpoint policy preserves 3000 s.");
 expired=1;
 assert(on_player_renew(&current,&output,&cancel,NULL)==-1 && api_last_error_access_expired());
 assert(api_reresolve_playback_cancel(current.item_id,NULL,&cancel,&output)==-1 && api_last_error_access_expired());
 puts("CONTROL: expired account is denied in both paths; not a permissible recovery bypass.");
}
