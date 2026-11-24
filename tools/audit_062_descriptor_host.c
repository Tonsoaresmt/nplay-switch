#include "api.h"
#include "net.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const char*BASE="https://fixture.invalid";
char g_token[640]="fixture-token";
static char*response;
Uint32 SDL_GetTicks(void){return 100;}
int SDL_AtomicGet(SDL_atomic_t*a){return a->value;}
void diag_network_event(const char*m,const char*p,long c,unsigned ms,size_t n){(void)m;(void)p;(void)c;(void)ms;(void)n;}
void membuf_free(struct membuf*m){free(m->data);memset(m,0,sizeof(*m));}
long net_request_timeout_cancel(const char*u,const char*m,const char*b,const char*t,struct membuf*out,const char**err,long connect,long total,SDL_atomic_t*cancel){
 (void)cancel;assert(strstr(u,"/api/stream/42")&&!strcmp(m,"POST")&&b&&t&&connect>0&&total>0);
 *err=NULL;out->len=strlen(response);assert(!out->limit||out->len<out->limit);out->data=malloc(out->len+1);assert(out->data);memcpy(out->data,response,out->len+1);return 200;
}
long net_request_timeout(const char*u,const char*m,const char*b,const char*t,struct membuf*out,const char**err,long connect,long total){return net_request_timeout_cancel(u,m,b,t,out,err,connect,total,NULL);}
int main(void){
#ifdef CORRECTED
 const size_t lengths[]={0,1,1500,1535,1536,2299};
 for(unsigned i=0;i<sizeof(lengths)/sizeof(*lengths);i++){
  size_t n=lengths[i];char url[2300];memset(url,'a',n);url[n]=0;
  if(n>=8)memcpy(url,"https://",8);
  cJSON*j=cJSON_CreateObject();cJSON_AddStringToObject(j,"play_url",url);response=cJSON_PrintUnformatted(j);cJSON_Delete(j);
  PlaybackSource out={0};int rc=api_resolve_playback(42,NULL,&out);
  if(n>=sizeof(out.play_url)){assert(rc!=0&&!out.play_url[0]&&api_last_error()[0]);}
  else {assert(rc==0);if(n>=8)assert(!strcmp(out.play_url,url));}
  cJSON_free(response);
 }
 const char*inputs[]={"/stream/a.m3u8","stream/a.mp4","https://fixture.invalid/a\r\nx:evil"};
 for(int i=0;i<3;i++){
  cJSON*j=cJSON_CreateObject();cJSON_AddStringToObject(j,"play_url",inputs[i]);response=cJSON_PrintUnformatted(j);cJSON_Delete(j);
  PlaybackSource out={0};int rc=api_resolve_playback(42,NULL,&out);
  if(i==2)assert(rc!=0&&!out.play_url[0]);else assert(rc==0&&strstr(out.play_url,"https://fixture.invalid/stream/a."));
  cJSON_free(response);
 }
 puts("PASS actual descriptor resolver: exact1535-byte URLs preserved, overflow and CRLF rejected, relative URLs and preparation accepted. HTTP mocked.");return 0;
#else
 char url[2300];memset(url,'a',sizeof(url));const char*prefix="https://cdn.fixture.invalid/film.mp4?signature=";
 memcpy(url,prefix,strlen(prefix));url[sizeof(url)-1]=0;
 cJSON*j=cJSON_CreateObject();assert(j);
 cJSON_AddStringToObject(j,"play_url",url);cJSON_AddStringToObject(j,"container","mp4");
 cJSON_AddStringToObject(j,"delivery","upstream");cJSON_AddNumberToObject(j,"source_id",1);cJSON_AddNumberToObject(j,"session_id",2);
 response=cJSON_PrintUnformatted(j);assert(response);cJSON_Delete(j);
 PlaybackSource out={0};int rc=api_resolve_playback(42,NULL,&out);
 assert(rc==0&&strlen(out.play_url)==sizeof(out.play_url)-1&&strlen(out.play_url)<strlen(url));
 assert(!strncmp(out.play_url,url,strlen(out.play_url)));
 printf("REPRODUCED descriptor URL truncation: input%zu chars -> accepted output%zu chars, resolver reports success; signed URL no longer identical. Real API C, HTTP fake, no live URL printed.\n",strlen(url),strlen(out.play_url));
 cJSON_free(response);return 0;
#endif
}
