// Real host libcurl + TLS loopback. URL mapping exists ONLY in this harness.
#define _POSIX_C_SOURCE 200809L
#include "connection_test.h"
#include "net.h"
#include <curl/curl.h>
#include <stdarg.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#undef curl_easy_setopt
static const char *origin,*ca;
static int requests;
int SDL_AtomicGet(SDL_atomic_t*v){return v->value;}
int SDL_AtomicSet(SDL_atomic_t*v,int n){int old=v->value;v->value=n;return old;}
Uint32 SDL_GetTicks(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (Uint32)(t.tv_sec*1000u+t.tv_nsec/1000000u);}
void net_configure_curl_isolated(CURL*c){
 curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
 curl_easy_setopt(c,CURLOPT_CAINFO,ca);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
}
static CURLcode mapped_setopt(CURL*c,CURLoption option,...){
 va_list args;va_start(args,option);CURLcode result;
 if(option==CURLOPT_URL){
  const char *source=va_arg(args,const char*);char url[1024];
  snprintf(url,sizeof(url),"%s/%s",origin,strstr(source,"/api/health")?"service":strstr(source,"bytes=4194304")?"download":"probe");
  requests++;result=curl_easy_setopt(c,option,url);
 }else if(option==CURLOPT_WRITEFUNCTION){
  size_t(*fn)(char*,size_t,size_t,void*)=va_arg(args,size_t(*)(char*,size_t,size_t,void*));result=curl_easy_setopt(c,option,fn);
 }else if(option==CURLOPT_XFERINFOFUNCTION){
  int(*fn)(void*,curl_off_t,curl_off_t,curl_off_t,curl_off_t)=va_arg(args,int(*)(void*,curl_off_t,curl_off_t,curl_off_t,curl_off_t));result=curl_easy_setopt(c,option,fn);
 }else if(option>=CURLOPTTYPE_OFF_T){result=curl_easy_setopt(c,option,va_arg(args,curl_off_t));}
 else if(option>=CURLOPTTYPE_OBJECTPOINT){result=curl_easy_setopt(c,option,va_arg(args,void*));}
 else result=curl_easy_setopt(c,option,va_arg(args,long));
 va_end(args);return result;
}
#define curl_easy_setopt mapped_setopt
#include "../source/connection_test.c"
#undef curl_easy_setopt
int main(int argc,char**argv){
 assert(argc==4);origin=argv[1];ca=argv[2];assert(!strncmp(origin,"https://127.0.0.1:",18));
 assert(!curl_global_init(CURL_GLOBAL_DEFAULT));
 ConnectionReport report;SDL_atomic_t cancel={0},phase={0},progress={0};
 connection_test_run("https://nplay.test",&cancel,&phase,&progress,&report);
 ConnectionSummary s;connection_test_summary(&report,&s);
 printf("requests=%d bytes=%zu elapsed=%u internet=%d service=%d download=%d mbps=%.2f partial=%d,%d assessment=%d\n",
        requests,report.bytes,report.elapsed_ms,s.internet_ok,s.service_ok,s.download_ok,s.mbps,
        report.downloads[0].partial,report.downloads[1].partial,s.assessment);
 assert(requests==9 && report.elapsed_ms<=CONNECTION_TEST_MAX_MS+1000u);
 assert(report.bytes<=2u*CONNECTION_DOWNLOAD_BYTES+7u*CONNECTION_PROBE_MAX);
 if(!strcmp(argv[3],"partial"))assert(s.download_ok==2 && report.downloads[0].partial && report.downloads[1].partial && s.assessment==CONNECTION_LOW_MARGIN);
 else if(!strcmp(argv[3],"tls"))assert(!s.internet_ok && !s.service_ok && !s.download_ok && s.assessment==CONNECTION_TLS_ERROR);
 else assert(s.internet_ok==4 && s.service_ok==3 && s.download_ok==2 && s.mbps>8 && !report.downloads[0].partial);
 curl_global_cleanup();
}
