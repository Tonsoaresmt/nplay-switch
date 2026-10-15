#include "api.h"
#include "playback_restart.h"
#include "net.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *BASE="https://nplay.test";
char g_token[640]="fixture";
static const char *body;
static long response=200;
static int requests,cancel_late;
Uint32 SDL_GetTicks(void){return 10;}
int SDL_AtomicGet(SDL_atomic_t *a){return a->value;}
void diag_network_event(const char *a,const char *b,long c,unsigned d,size_t e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void membuf_free(struct membuf *m){free(m->data);memset(m,0,sizeof(*m));}
long net_request_timeout(const char *url,const char *method,const char *data,const char *bearer,
 struct membuf *out,const char **error,long connect,long timeout) {
 (void)url;(void)method;(void)data;(void)bearer;(void)out;(void)error;(void)connect;(void)timeout;
 assert(!"unexpected network route");return -1;
}
long net_request_timeout_cancel(const char *url,const char *method,const char *data,const char *bearer,
 struct membuf *out,const char **error,long connect,long timeout,SDL_atomic_t *cancel){
 assert(!strcmp(url,"https://nplay.test/api/stream/hot/1234567890abcdef_fixture/seek?t=600.250"));
 assert(out->limit==16384&&connect==3&&timeout==8&&!strcmp(method,"GET")&&!data&&!strcmp(bearer,"fixture"));
 *error=NULL;requests++;
 if(body){out->len=strlen(body);out->data=malloc(out->len+1);assert(out->data);memcpy(out->data,body,out->len+1);}
 if(cancel_late)cancel->value=1;
 return response;
}
static PlaybackSource fixture(void){
 PlaybackSource s={.item_id=42,.source_id=7,.sequential_stream=1,.restart_seek=1};
 strcpy(s.container,"mp4");strcpy(s.delivery_str,"debrid");strcpy(s.hot_session_id,"1234567890abcdef_fixture");
 strcpy(s.play_url,"https://nplay.test/api/media/debrid/1234567890abcdef_fixture/video?token=fixture&quality=1080");
 return s;
}
int main(void){
 PlaybackSource s=fixture(),out={.item_id=99};SDL_atomic_t cancel={0};
 body="{\"ok\":true,\"mode\":\"restart\",\"start\":598,\"duration\":1440}";
 assert(!api_prepare_restart_cancel(&s,600.25,&cancel,&out));
 assert(out.restart_base==598&&out.restart_duration==1440&&out.item_id==42);
 assert(strstr(out.play_url,"?token=fixture&quality=1080&t=600.250"));
 assert(!strstr(s.play_url,"&t="));
 // Same zero-based clip timestamp is 598s on the full-title clock.
 assert(playback_restart_clock(0,0,out.restart_base)==598);
 assert(playback_restart_clock(2.25,0,out.restart_base)==600.25);
 const long errors[]={503,404,410,401,403,-28,-60};
 for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);i++){
  response=errors[i];out.item_id=99;
  assert(api_prepare_restart_cancel(&s,600.25,&cancel,&out)&&out.item_id==99);
 }
 response=200;const char *bad[]={"{}","{\"ok\":true,\"mode\":\"range\",\"start\":598}",
 "{\"ok\":true,\"mode\":\"restart\",\"start\":0}",
 "{\"ok\":true,\"mode\":\"restart\",\"start\":650}",
 "{\"ok\":true,\"mode\":\"restart\",\"start\":598,\"duration\":10}",
 "{\"ok\":true,\"mode\":\"restart\",\"start\":\"598\"}"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
  body=bad[i];out.item_id=99;assert(api_prepare_restart_cancel(&s,600.25,&cancel,&out)&&out.item_id==99);
 }
 body="{\"ok\":true,\"mode\":\"restart\",\"start\":598}";
 cancel_late=1;out.item_id=99;assert(api_prepare_restart_cancel(&s,600.25,&cancel,&out)&&out.item_id==99);
 int before=requests;assert(api_prepare_restart_cancel(&s,600.25,&cancel,&out)&&requests==before);
 cancel.value=cancel_late=0;assert(!api_prepare_restart_cancel(&s,0,&cancel,&out)&&requests==before);
 assert(out.restart_base==0&&strstr(out.play_url,"&t=0.000"));
 char plan[1024],video[1536];
 const double invalid[]={NAN,INFINITY,-1,86401};
 for(unsigned i=0;i<4;i++)assert(!playback_restart_urls(&s,BASE,invalid[i],plan,sizeof(plan),video,sizeof(video)));
 const char *urls[]={"https://evil.test/api/media/debrid/1234567890abcdef_fixture/video",
 "https://nplay.test/api/media/debrid/another_session_id/video",
 "https://nplay.test/api/media/debrid/1234567890abcdef_fixture/video/extra",
 "https://nplay.test/api/media/debrid/1234567890abcdef_fixture/video?t=10",
 "https://nplay.test/api/media/debrid/1234567890abcdef_fixture/video?%74=10",
 "https://nplay.test/api/media/debrid/1234567890abcdef_fixture/video#t=10"};
 for(unsigned i=0;i<sizeof(urls)/sizeof(urls[0]);i++){
  PlaybackSource bad_source=s;snprintf(bad_source.play_url,sizeof(bad_source.play_url),"%s",urls[i]);
  assert(!playback_restart_urls(&bad_source,BASE,600,plan,sizeof(plan),video,sizeof(video)));
 }
 assert(!playback_restart_urls(&s,BASE,600,plan,10,video,sizeof(video)));
 assert(!playback_restart_urls(&s,BASE,600,plan,sizeof(plan),video,10));
 s.restart_seek=0;assert(!playback_restart_supported(&s,BASE));
 puts("PASS real restart API: bounded plan, full-title base, preserved signed query/source, errors/cancel retain output, zero explicit, hostile URL/SID/NaN/truncation rejected.");
 return 0;
}
