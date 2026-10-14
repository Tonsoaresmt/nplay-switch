#define _POSIX_C_SOURCE 200809L
#include "connection_test.h"
#include "connection_test_ui.h"
#include "settings_layout.h"
#include "net.h"
#include "diag.h"
#include "ui.h"
#include "text.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#undef curl_easy_setopt
#undef curl_easy_getinfo
typedef struct {
 const char *url;long peer,host,protocols,follow,timeout,connect,http_version,decode;
 curl_off_t maximum;
 size_t(*write)(char*,size_t,size_t,void*);void*write_arg;
 int(*progress)(void*,curl_off_t,curl_off_t,curl_off_t,curl_off_t);void*progress_arg;
 long code;const char *content_type;double pre,first,total;
} FakeCurl;
enum {GOOD,SLOW,VARIABLE,SERVICE_DOWN,EXTERNAL_DOWN,TLS_FAIL,OVERSIZE,HTML,
      TRUNCATED,PARTIAL,DEADLINE,INIT_FAIL,CANCELLED,MISSING_TIMING,SERVICE_SLOW,REDIRECT};
static int scenario,opens,closes,calls,gui_mode,net_headers,thread_count,thread_joins;
static Uint32 fake_ticks;
static SDL_atomic_t*active_cancel;
static pthread_t render_thread;
int SDL_AtomicGet(SDL_atomic_t*v){return __atomic_load_n(&v->value,__ATOMIC_SEQ_CST);}
int SDL_AtomicSet(SDL_atomic_t*v,int n){return __atomic_exchange_n(&v->value,n,__ATOMIC_SEQ_CST);}
Uint32 SDL_GetTicks(void){return __atomic_load_n(&fake_ticks,__ATOMIC_SEQ_CST);}
static void nap(unsigned ms){struct timespec t={0,(long)ms*1000000};nanosleep(&t,NULL);}
CURL*curl_easy_init(void){if(scenario==INIT_FAIL)return NULL;opens++;return (CURL*)calloc(1,sizeof(FakeCurl));}
void curl_easy_reset(CURL*c){memset(c,0,sizeof(FakeCurl));}
void curl_easy_cleanup(CURL*c){closes++;free(c);}
void net_configure_curl_isolated(CURL*c){assert(c);net_headers++;}
CURLcode curl_easy_setopt(CURL*c,CURLoption option,...){
 FakeCurl*f=(FakeCurl*)c;va_list v;va_start(v,option);
 switch(option){
 case CURLOPT_URL:f->url=va_arg(v,const char*);break;
 case CURLOPT_USERAGENT:assert(!strcmp(va_arg(v,const char*),"Nplay-Connection-Test"));break;
 case CURLOPT_HTTP_VERSION:f->http_version=va_arg(v,long);break;
 case CURLOPT_PROTOCOLS:f->protocols=va_arg(v,long);break;
 case CURLOPT_FOLLOWLOCATION:f->follow=va_arg(v,long);break;
 case CURLOPT_SSL_VERIFYPEER:f->peer=va_arg(v,long);break;
 case CURLOPT_SSL_VERIFYHOST:f->host=va_arg(v,long);break;
 case CURLOPT_CONNECTTIMEOUT_MS:f->connect=va_arg(v,long);break;
 case CURLOPT_TIMEOUT_MS:f->timeout=va_arg(v,long);break;
 case CURLOPT_MAXFILESIZE_LARGE:f->maximum=va_arg(v,curl_off_t);break;
 case CURLOPT_HTTP_CONTENT_DECODING:f->decode=va_arg(v,long);break;
 case CURLOPT_ACCEPT_ENCODING:assert(!strcmp(va_arg(v,const char*),"identity"));break;
 case CURLOPT_WRITEFUNCTION:f->write=va_arg(v,size_t(*)(char*,size_t,size_t,void*));break;
 case CURLOPT_WRITEDATA:f->write_arg=va_arg(v,void*);break;
 case CURLOPT_XFERINFOFUNCTION:f->progress=va_arg(v,int(*)(void*,curl_off_t,curl_off_t,curl_off_t,curl_off_t));break;
 case CURLOPT_XFERINFODATA:f->progress_arg=va_arg(v,void*);break;
 case CURLOPT_NOPROGRESS:assert(va_arg(v,long)==0);break;
 default:assert(!"Unexpected option, credentials or cookie/share");
 }va_end(v);return CURLE_OK;
}
CURLcode curl_easy_getinfo(CURL*c,CURLINFO info,...){
 FakeCurl*f=(FakeCurl*)c;va_list v;va_start(v,info);
 if(info==CURLINFO_RESPONSE_CODE)*va_arg(v,long*)=f->code;
 else if(info==CURLINFO_CONTENT_TYPE)*va_arg(v,char**)= (char*)f->content_type;
 else {*va_arg(v,double*)=info==CURLINFO_PRETRANSFER_TIME?f->pre:info==CURLINFO_STARTTRANSFER_TIME?f->first:f->total;}
 va_end(v);return CURLE_OK;
}
CURLcode curl_easy_perform(CURL*c){
 FakeCurl*f=(FakeCurl*)c;int step=calls++%9;
 if(gui_mode){assert(!pthread_equal(pthread_self(),render_thread));nap(4);}
 assert(f->peer==1 && f->host==2 && f->protocols==CURLPROTO_HTTPS && !f->follow && !f->decode);
 assert(f->http_version==CURL_HTTP_VERSION_1_1 && f->connect==1500 && f->timeout>0 && f->timeout<=6000);
 assert(f->write && f->progress && f->progress_arg);
 int service=strstr(f->url,"nplay.test")!=NULL,download=strstr(f->url,"bytes=4194304")!=NULL;
 assert((step>=4&&step<7)==service && (step>=7)==download);
 assert(!strncmp(f->url,"https://",8));
 f->code=200;f->content_type=service?"application/json; charset=utf-8":"application/octet-stream";
 f->pre=.1;f->first=.15;f->total=download?.65:.151;
 if(service)f->first=.22;
 if(scenario==SLOW && download)f->total=6.15;
 if(scenario==VARIABLE && step==1)f->first=.7;
 if(scenario==SERVICE_DOWN && service)f->code=503;
 if(scenario==SERVICE_SLOW && service)f->first=1.8;
 if(scenario==REDIRECT)f->code=302;
 if(scenario==EXTERNAL_DOWN && !service)f->code=403;
 if(!download)f->total=f->first+.001;
 if(scenario==TLS_FAIL)return CURLE_PEER_FAILED_VERIFICATION;
 if(scenario==DEADLINE)__atomic_add_fetch(&fake_ticks,30001,__ATOMIC_SEQ_CST);
 else __atomic_add_fetch(&fake_ticks,50,__ATOMIC_SEQ_CST);
 if(scenario==CANCELLED)SDL_AtomicSet(active_cancel,1);
 if(f->progress(f->progress_arg,0,0,0,0))return CURLE_ABORTED_BY_CALLBACK;
 size_t count=download?CONNECTION_DOWNLOAD_BYTES:service?1000:0;
 if(scenario==OVERSIZE)count=(size_t)f->maximum+1;
 if(scenario==HTML && download){count=1000;f->content_type="text/html";}
 if(scenario==TRUNCATED && download)count=1000;
 if(scenario==PARTIAL && download){count=1024u*1024u;f->total=6.15;}
 if(scenario==MISSING_TIMING)f->first=f->pre=f->total=0;
 char data[65536]={0};
 size_t sent=0;
 while(sent<count){size_t part=count-sent;if(part>sizeof(data))part=sizeof(data);
  if(f->write(data,1,part,f->write_arg)!=part)return CURLE_WRITE_ERROR;
  sent+=part;if(f->progress(f->progress_arg,count,sent,0,0))return CURLE_ABORTED_BY_CALLBACK;
 }
 return scenario==PARTIAL&&download?CURLE_OPERATION_TIMEDOUT:CURLE_OK;
}

struct SDL_Thread {pthread_t id;int(*fn)(void*);void*arg;};
static void*thread_worker(void*opaque){SDL_Thread*t=opaque;t->fn(t->arg);return NULL;}
static int fail_thread;
SDL_Thread*SDL_CreateThread(int(*fn)(void*),const char*name,void*arg){
 assert(!strcmp(name,"connection-test"));if(fail_thread)return NULL;
 SDL_Thread*t=calloc(1,sizeof(*t));assert(t);t->fn=fn;t->arg=arg;thread_count++;assert(!pthread_create(&t->id,NULL,thread_worker,t));return t;
}
void SDL_WaitThread(SDL_Thread*t,int*rc){(void)rc;assert(!pthread_join(t->id,NULL));thread_joins++;free(t);}
static int frames,action1,action2,quit_event;
static const char*seen_text;
static int seen_partial;
void SDL_Delay(Uint32 ms){assert(ms==16);frames++;nap(1);}
int SDL_PollEvent(SDL_Event*e){
 memset(e,0,sizeof(*e));
 int action=frames==1?action1:frames==5?action2:0;
 if(action){
  if(action==5){e->type=SDL_QUIT;quit_event=1;}
  else if(action==3||action==4){e->type=SDL_FINGERDOWN;e->tfinger.x=action==3?.5f:.92f;e->tfinger.y=action==3?.87f:.05f;}
  else{e->type=SDL_JOYBUTTONDOWN;e->jbutton.button=action==1?JOY_A:JOY_B;}
  if(frames==1)action1=0;else action2=0;
  return 1;
 }
 if(frames>60 && !quit_event){quit_event=1;e->type=SDL_JOYBUTTONDOWN;e->jbutton.button=JOY_B;return 1;}
 return 0;
}
int SDL_SetRenderDrawColor(SDL_Renderer*r,unsigned char a,unsigned char b,unsigned char c,unsigned char d){(void)r;(void)a;(void)b;(void)c;(void)d;assert(pthread_equal(pthread_self(),render_thread));return 0;}
int SDL_RenderClear(SDL_Renderer*r){(void)r;return 0;}
void SDL_RenderPresent(SDL_Renderer*r){(void)r;}
const SDL_Color C_BG={0},C_BAR={0},C_TEXT={0},C_MUT={0},C_ACC={0},C_ACC2={0},C_GREEN={0};
SDL_Renderer*gRen=(SDL_Renderer*)1;
void fill_rect(int x,int y,int w,int h,SDL_Color c){(void)c;assert(x>=0&&y>=0&&x+w<=1280&&y+h<=720);}
void ui_header(const char*a,const char*b,const char*c){(void)a;(void)b;(void)c;}
int ui_header_action_hit(int x,int y){return x>1100 && y<80;}
void ui_panel(int x,int y,int w,int h,SDL_Color c){fill_rect(x,y,w,h,c);}
void ui_popcorn_draw(SDL_Renderer*r,int x,int y,int size){(void)r;(void)x;(void)y;(void)size;}
void ui_footer(const char*s){(void)s;}
void ui_progress(int x,int y,int w,int value,SDL_Color c){(void)x;(void)y;(void)w;(void)c;assert(value>=0 && value<=100);}
int text_draw(SDL_Renderer*r,const char*s,int x,int y,SDL_Color c,int style){(void)r;(void)s;(void)x;(void)y;(void)c;(void)style;return 0;}
void text_clip(const char*s,int x,int y,SDL_Color c,int style,int width){(void)x;(void)y;(void)c;(void)style;(void)width;if(strstr(s,"Nao foi possivel iniciar"))seen_text=s;if(strstr(s,"parcial(is)"))seen_partial=1;}
int text_center_at(const char*s,int x,int w,int y,SDL_Color c,int style){(void)x;(void)w;(void)y;(void)c;(void)style;assert(s);return 0;}
void diag_network_event(const char*m,const char*p,long code,unsigned ms,size_t bytes){(void)code;(void)ms;(void)bytes;assert(!strcmp(m,"TEST") && !strncmp(p,"/diagnostics/",13));assert(!strchr(p,'?'));}
typedef struct {const char*base;SDL_atomic_t cancel,done,phase,progress;ConnectionReport report;} ConnectionJob;
static ConnectionSummary g_previous_connection;
static int g_previous_connection_valid;
#include "connection_ui.inc"
static int g_settings_section;
#include "connection_settings.inc"
static void reset(int mode){scenario=mode;calls=opens=closes=net_headers=0;fake_ticks=100;}
static ConnectionSummary run(int mode){
 reset(mode);SDL_atomic_t cancel={0},phase={0},progress={0};active_cancel=&cancel;ConnectionReport report;
 connection_test_run("https://nplay.test",&cancel,&phase,&progress,&report);
 assert(opens==closes && report.bytes<=2u*CONNECTION_DOWNLOAD_BYTES+7u*CONNECTION_PROBE_MAX);
 ConnectionSummary summary;connection_test_summary(&report,&summary);return summary;
}
int main(void){
 render_thread=pthread_self();ConnectionSummary s=run(GOOD);
 assert(s.assessment==CONNECTION_OK && s.internet_ok==4 && s.service_ok==3 && s.download_ok==2 && s.mbps>60);
 assert(run(SLOW).assessment==CONNECTION_LOW_MARGIN);
 assert(run(VARIABLE).assessment==CONNECTION_VARIABLE);
 assert(run(SERVICE_DOWN).assessment==CONNECTION_SERVICE_UNAVAILABLE);
 assert(run(SERVICE_SLOW).assessment==CONNECTION_SERVICE_SLOW);
 assert(run(REDIRECT).assessment==CONNECTION_NO_RESULT);
 assert(run(EXTERNAL_DOWN).assessment==CONNECTION_EXTERNAL_UNAVAILABLE);
 assert(run(TLS_FAIL).assessment==CONNECTION_TLS_ERROR);
 assert(!run(OVERSIZE).download_ok && !run(HTML).download_ok && !run(TRUNCATED).download_ok);
 assert(run(PARTIAL).download_ok==2 && run(PARTIAL).assessment==CONNECTION_LOW_MARGIN);
 assert(run(PARTIAL).partial_downloads==2);
 assert(run(DEADLINE).assessment==CONNECTION_NO_RESULT && calls==1);
 assert(run(INIT_FAIL).assessment==CONNECTION_LOCAL_ERROR);
 assert(run(CANCELLED).assessment==CONNECTION_NO_RESULT && calls==1);
 assert(run(MISSING_TIMING).assessment==CONNECTION_NO_RESULT);
 puts("PASS transport: bounded discard, HTTPS/host/peer, no credentials/redirects, timeout/503/403/TLS, partial/HTML/truncated, cleanup.");
 reset(GOOD);SDL_atomic_t cancel={0},phase={0},progress={0};active_cancel=&cancel;ConnectionReport report;
 fake_ticks=UINT32_MAX-100;connection_test_run("https://nplay.test",&cancel,&phase,&progress,&report);assert(calls==9 && report.elapsed_ms==450);
 connection_test_run("http://nplay.test",&cancel,&phase,&progress,&report);assert(!report.bytes && calls==9);
 puts("PASS timing/model: finite Mbps, partial marked, median/range, wrap-safe deadline, independent service vs external diagnosis.");
 for(int section=0;section<4;section++){g_settings_section=section;int count=settings_row_count();assert(count==(section==3?5:section==1?4:3));
  assert(SETTINGS_NAV[section] && SETTINGS_DETAILS[section][0]);
  for(int i=0;i<count;i++){int y=settings_row_y(section,i);assert(settings_row_at(section,count,500,y)==i);assert(settings_row_at(section,count,500,y+settings_row_height(section))==-1);assert(y+settings_row_height(section)<=620);}
 }assert(settings_row_at(3,5,500,612)==4);assert(!strcmp(SETTINGS_ROWS[3][2],"Teste de internet"));
 puts("PASS settings: 5 rows fit 720p; drawn/touch targets, gaps and last Close row agree.");
 gui_mode=1;
 for(int i=0;i<7;i++){
  reset(i==6?PARTIAL:GOOD);frames=quit_event=fail_thread=seen_partial=0;seen_text=NULL;
  action1=i==0?2:i==2?3:1;action2=i==1?2:i==2?4:i==3?5:0;
  if(i==4)fail_thread=1;
  int running=1;connection_test_show(gRen,"https://nplay.test",&running);
  assert(thread_count==thread_joins && opens==closes);
  if(i==0)assert(!calls); // B on confirmation: zero charged download.
  if(i==3)assert(!running);
  if(i==4)assert(seen_text && !calls);
  if(i==6)assert(seen_partial);
 }
 puts("PASS UI: explicit consent, A/touch, B/header/quit, creation failure, real pthread done/join, renderer stays on owner.");
}
