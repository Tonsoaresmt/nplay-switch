#include "api.h"
#include "net.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const char *BASE="https://fixture.invalid";
char g_token[640]="fixture-token";
static int calls;
static long response;
static SDL_atomic_t *expected_cancel;
Uint32 SDL_GetTicks(void){return 100;}
int SDL_AtomicGet(SDL_atomic_t*a){return a->value;}
void diag_network_event(const char*m,const char*p,long c,unsigned ms,size_t n){(void)m;(void)c;(void)ms;(void)n;assert(!strcmp(p,"/api/stream/session/123/stop"));}
void membuf_free(struct membuf*m){free(m->data);memset(m,0,sizeof(*m));}
long net_request_timeout_cancel(const char*u,const char*m,const char*b,const char*t,struct membuf*out,const char**err,long connect,long total,SDL_atomic_t*cancel){
 (void)out;*err=NULL;assert(!strcmp(u,"https://fixture.invalid/api/stream/session/123/stop"));
 assert(!strcmp(m,"POST")&&!strcmp(b,"{}")&&!strcmp(t,g_token)&&connect==3&&total==6&&cancel==expected_cancel);
 calls++;return response;
}
long net_request_timeout(const char*u,const char*m,const char*b,const char*t,struct membuf*out,const char**err,long connect,long total){
 return net_request_timeout_cancel(u,m,b,t,out,err,connect,total,NULL);
}
#include "session_stop_glue.inc"
int main(void){
 SDL_atomic_t cancel={0};expected_cancel=&cancel;
 long results[]={200,404,403,503,-60,-42};
 for(unsigned i=0;i<sizeof(results)/sizeof(*results);i++){
  response=results[i];int prior=calls;int rc=on_player_stop(999,123,&cancel,NULL);
  assert(rc==(response==200?0:-1)&&calls==prior+1); // Never fallback or send item ID as SID.
 }
 int prior=calls;assert(!on_player_stop(999,0,&cancel,NULL)&&calls==prior);
 puts("PASS NRO exact stop: actual callback/API, correct SID, cancel/timeout/TLS/404/403/503 fail without broad fallback; unknown SID never sends a stop.");return 0;
}
