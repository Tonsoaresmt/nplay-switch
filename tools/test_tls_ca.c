#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#define __SWITCH__ 1
typedef uint32_t Result;
typedef uint32_t u32;
typedef int CURLcode;
enum { CURLE_OK=0, CURLE_OUT_OF_MEMORY=27, CURLE_PEER_FAILED_VERIFICATION=60,
       CURLE_SSL_CACERT_BADFILE=77, SslCertificateFormat_Pem=1 };
enum { CURLOPT_NOSIGNAL, CURLOPT_TCP_KEEPALIVE, CURLOPT_CAINFO, CURLOPT_CAPATH,
       CURLOPT_SSL_CTX_FUNCTION, CURLOPT_SSL_VERIFYPEER, CURLOPT_SSL_VERIFYHOST };
typedef struct { int calls; Result fail; } SslContext;
typedef CURLcode (*Callback)(void *, void *, void *);
typedef struct { long peer,host; const char *ca,*path; Callback cb; } CURL;
typedef struct { const char *ssl_version; } curl_version_info_data;
#define CURLVERSION_NOW 0
static curl_version_info_data version={"libnx"};
static const curl_version_info_data *curl_version_info(int age) { (void)age; return &version; }
#define R_FAILED(x) ((x)!=0)
#include "tls_bundle.inc"
static int allocation_fail;
static void *test_malloc(size_t n) { return allocation_fail ? NULL : malloc(n); }
#define malloc test_malloc
static Result sslContextImportServerPki(SslContext *ctx, const void *pem,
                                      u32 size, int format, void *id) {
    assert(size==cacert_bin_size+1 && format==SslCertificateFormat_Pem && !id);
    assert(!memcmp(pem,cacert_bin,cacert_bin_size));
    assert(((const unsigned char *)pem)[cacert_bin_size]==0);
    ctx->calls++;
    return ctx->fail;
}
static const char *curl_easy_strerror(CURLcode code) { (void)code; return "other"; }
#include "tls_ca.inc"
#undef malloc
// The real curl API is variadic; dispatch with typed helpers to test values.
static void opt_long(CURL *c,int o,long v) { if(o==CURLOPT_SSL_VERIFYPEER)c->peer=v; if(o==CURLOPT_SSL_VERIFYHOST)c->host=v; }
static void opt_str(CURL *c,int o,const char *v) { if(o==CURLOPT_CAINFO)c->ca=v; if(o==CURLOPT_CAPATH)c->path=v; }
static void opt_cb(CURL *c,int o,CURLcode (*v)(CURL *,void *,void *)) { (void)o; c->cb=(Callback)v; }
#define curl_easy_setopt(c,o,v) _Generic((v),long:opt_long, char*:opt_str, const char*:opt_str, void*:opt_str, default:opt_cb)(c,o,v)
#include "tls_config.inc"
static void *thread_test(void *arg) {
    Result fail=(Result)(uintptr_t)arg;
    SslContext ctx={0,fail}; CURL curl={0};
    net_configure_curl_isolated(&curl);
    assert(curl.cb(&curl,&ctx,NULL)==CURLE_SSL_CACERT_BADFILE);
    assert(g_tls_import_error==fail);
    char expected[16]; snprintf(expected,sizeof(expected),"%08X",fail);
    assert(strstr(net_transport_error(CURLE_SSL_CACERT_BADFILE),expected));
    return NULL;
}
int main(void) {
    CURL curl={0}; SslContext ctx={0};
    // A missing/read-only/corrupt SD cannot influence this path: no file I/O.
    assert(switch_ca_context(&curl,&ctx,NULL)==CURLE_OUT_OF_MEMORY);
    allocation_fail=1; assert(prepare_ca_memory()==-1 && !g_ca_pem);
    allocation_fail=0; assert(prepare_ca_memory()==0);
    unsigned char *first=g_ca_pem;
    assert(prepare_ca_memory()==0 && first==g_ca_pem);
    version.ssl_version="OpenSSL";
    assert(switch_ca_context(&curl,&ctx,NULL)==77 && !ctx.calls);
    version.ssl_version=NULL;
    assert(switch_ca_context(&curl,&ctx,NULL)==77 && !ctx.calls);
    version.ssl_version="libnx";
    assert(switch_ca_context(&curl,NULL,NULL)==CURLE_SSL_CACERT_BADFILE);
    net_configure_curl_isolated(NULL);
    net_configure_curl_isolated(&curl);
    assert(curl.peer==1 && curl.host==2 && !curl.path);
    assert(!strcmp(curl.ca,"nplay-embedded-ca-only") && curl.cb);
    assert(curl.cb(&curl,&ctx,NULL)==CURLE_OK && ctx.calls==1);
    for(int i=0;i<1000;i++)assert(curl.cb(&curl,&ctx,NULL)==CURLE_OK);
    ctx.fail=0x1234;
    assert(curl.cb(&curl,&ctx,NULL)==77 && g_tls_import_error==0x1234);
    assert(strstr(net_transport_error(77),"00001234"));
    ctx.fail=0; assert(curl.cb(&curl,&ctx,NULL)==0 && !g_tls_import_error);
    assert(strstr(net_transport_error(77),"(77)"));
    assert(strstr(net_transport_error(60),"(60)"));
    assert(!strcmp(net_transport_error(5),"other"));
    pthread_t threads[4];
    for(int i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,thread_test,(void *)(uintptr_t)(0x8000+i)));
    for(int i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
    assert(!g_tls_import_error);
    free(g_ca_pem); g_ca_pem=NULL;
    assert(curl.cb(&curl,&ctx,NULL)==CURLE_OUT_OF_MEMORY);
    assert(prepare_ca_memory()==0); free(g_ca_pem); g_ca_pem=NULL;
    puts("TLS CA: real embedded bundle, 1000 contexts, 4 threads, allocation/import failures and fail-closed verification passed (native SSL stub).");
}
