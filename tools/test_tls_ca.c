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
// Modelo do servico SSL do Horizon. Cada campo liga um comportamento hipotetico
// que ja foi (ou pode ser) a causa de "importacao de certificados falhou".
typedef struct {
    Result fail;        // toda chamada falha com este resultado
    int reject_nul;     // parser rejeita buffer cujo ultimo byte e NUL (regressao da 0.12.51)
    int max_certs;      // limite de certificados aceitos por contexto (0 = sem limite)
    int single_only;    // aceita apenas buffers com exatamente 1 certificado
    int calls;          // chamadas de importacao recebidas
    int certs;          // certificados aceitos ate agora
    u32 first_size;     // tamanho enviado na 1a chamada
    int first_is_full;  // 1a chamada = bundle completo, byte a byte
} SslContext;
typedef CURLcode (*Callback)(void *, void *, void *);
typedef struct { long peer,host; const char *ca,*path; Callback cb; } CURL;
typedef struct { const char *ssl_version; } curl_version_info_data;
#define CURLVERSION_NOW 0
static curl_version_info_data version={"libnx"};
static const curl_version_info_data *curl_version_info(int age) { (void)age; return &version; }
#define R_FAILED(x) ((x)!=0)
#include "tls_bundle.inc"
static int alloc_budget=-1;   // <0: ilimitado; N: as N proximas alocacoes passam, depois falham
static void *test_malloc(size_t n) {
    if (alloc_budget==0) return NULL;
    if (alloc_budget>0) alloc_budget--;
    return malloc(n);
}
#define malloc test_malloc
static int count_certs(const unsigned char *p, u32 n) {
    static const char m[]="-----BEGIN CERTIFICATE-----"; int c=0;
    for (u32 i=0; i+sizeof(m)-1<=n; i++) if (!memcmp(p+i,m,sizeof(m)-1)) c++;
    return c;
}
static Result sslContextImportServerPki(SslContext *ctx, const void *pem,
                                      u32 size, int format, void *id) {
    const unsigned char *b=pem;
    assert(format==SslCertificateFormat_Pem && !id && size>0);
    if (ctx->calls==0) {
        ctx->first_size=size;
        ctx->first_is_full=size==cacert_bin_size && !memcmp(pem,cacert_bin,size);
    }
    ctx->calls++;
    if (ctx->fail) return ctx->fail;
    if (ctx->reject_nul && b[size-1]==0) return 0xBAD1;
    int certs=count_certs(b,size);
    if (ctx->single_only && certs!=1) return 0xBAD3;
    if (ctx->max_certs && ctx->certs+certs>ctx->max_certs) return 0xBAD2;
    ctx->certs+=certs;
    return 0;
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
static void reset_ca(void) {
    free(g_ca_pem); g_ca_pem=NULL;
    free(g_ca_sub); g_ca_sub=NULL; g_ca_sub_len=0; g_ca_blk_n=0;
}
static void *thread_test(void *arg) {
    Result fail=(Result)(uintptr_t)arg;
    SslContext ctx={.fail=fail}; CURL curl={0};
    net_configure_curl_isolated(&curl);
    assert(curl.cb(&curl,&ctx,NULL)==CURLE_SSL_CACERT_BADFILE);
    // Todos os estagios falharam: o codigo reportado e o da PRIMEIRA falha (E1), por thread.
    assert(g_tls_import_error==fail && g_tls_import_stage==3);
    char expected[32]; snprintf(expected,sizeof(expected),"%08X E3",fail);
    assert(strstr(net_transport_error(CURLE_SSL_CACERT_BADFILE),expected));
    return NULL;
}
// Procura "\n<nome>\n=====" no bundle: o nome existe como raiz do Mozilla?
static int bundle_has_root(const char *name) {
    char needle[160]; snprintf(needle,sizeof(needle),"\n%s\n=====",name);
    size_t n=strlen(needle);
    for (size_t i=0; i+n<=cacert_bin_size; i++) if (!memcmp(cacert_bin+i,needle,n)) return 1;
    return 0;
}
int main(void) {
    CURL curl={0}; SslContext ctx={0};
    // A missing/read-only/corrupt SD cannot influence this path: no file I/O.
    assert(switch_ca_context(&curl,&ctx,NULL)==CURLE_OUT_OF_MEMORY);
    alloc_budget=0; assert(prepare_ca_memory()==-1 && !g_ca_pem && !g_ca_sub);
    alloc_budget=-1; assert(prepare_ca_memory()==0);
    unsigned char *first=g_ca_pem;
    assert(prepare_ca_memory()==0 && first==g_ca_pem);

    // ---- subconjunto prioritario: PEM puro, dentro do limite e com as raizes dos servidores do app ----
    unsigned expected_roots=0;
    for (size_t i=0;i<sizeof(k_ca_priority)/sizeof(k_ca_priority[0]);i++) expected_roots+=bundle_has_root(k_ca_priority[i]);
    assert(expected_roots>=50 && expected_roots<=CA_SUB_MAX);
    assert(g_ca_sub && g_ca_blk_n==expected_roots && g_ca_sub_len<cacert_bin_size);
    static const char *const must[]={"GTS Root R4","ISRG Root X1","USERTrust ECC Certification Authority"};
    for (size_t i=0;i<sizeof(must)/sizeof(must[0]);i++) assert(bundle_has_root(must[i]));
    assert(count_certs(g_ca_sub,g_ca_sub_len)==(int)g_ca_blk_n);
    assert(!memcmp(g_ca_sub,"-----BEGIN CERTIFICATE-----",27) && g_ca_sub[g_ca_sub_len]==0);
    assert(!strstr((const char *)g_ca_sub,"####") && !strstr((const char *)g_ca_sub,"====") &&
           !strstr((const char *)g_ca_sub,"Root"));   // sem comentarios nem nomes: so PEM
    unsigned covered=0;
    for (unsigned i=0;i<g_ca_blk_n;i++) {
        assert(g_ca_blk[i].off+g_ca_blk[i].len<=g_ca_sub_len);
        assert(!memcmp(g_ca_sub+g_ca_blk[i].off,"-----BEGIN CERTIFICATE-----",27));
        assert(g_ca_sub[g_ca_blk[i].off+g_ca_blk[i].len-1]=='\n');
        assert(count_certs(g_ca_sub+g_ca_blk[i].off,g_ca_blk[i].len)==1);
        covered+=g_ca_blk[i].len;
    }
    assert(covered==g_ca_sub_len);

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

    // ---- E1 feliz: bundle completo, tamanho EXATO (sem o NUL), uma unica chamada ----
    assert(curl.cb(&curl,&ctx,NULL)==CURLE_OK && ctx.calls==1);
    assert(ctx.first_is_full && ctx.first_size==cacert_bin_size && g_tls_import_stage==1 && !g_tls_import_error);
    for(int i=0;i<1000;i++){ SslContext c={0}; assert(curl.cb(&curl,&c,NULL)==CURLE_OK && c.calls==1); }
    // Regressao da 0.12.51: um servico que rejeita NUL final nao pode mais ser acionado.
    { SslContext c={.reject_nul=1}; assert(curl.cb(&curl,&c,NULL)==CURLE_OK && c.calls==1 && !g_tls_import_error); }

    // ---- E2: limite de certificados (71) derruba o bundle de 121, o subconjunto passa ----
    { SslContext c={.max_certs=71}; assert(curl.cb(&curl,&c,NULL)==CURLE_OK);
      assert(c.calls==2 && c.certs==(int)g_ca_blk_n && g_tls_import_stage==2 && !g_tls_import_error); }

    // ---- E3: servico que so aceita um certificado por vez ----
    { SslContext c={.single_only=1}; assert(curl.cb(&curl,&c,NULL)==CURLE_OK);
      assert(c.calls==2+(int)g_ca_blk_n && c.certs==(int)g_ca_blk_n && g_tls_import_stage==3 && !g_tls_import_error); }

    // ---- tudo falha: falha fechada e codigo da PRIMEIRA falha, curto o bastante p/ a tela de login ----
    { SslContext c={.fail=0x1234}; assert(curl.cb(&curl,&c,NULL)==77);
      assert(c.calls==2+(int)g_ca_blk_n && g_tls_import_error==0x1234 && g_tls_import_stage==3);
      const char *msg=net_transport_error(77);
      assert(!strcmp(msg,"TLS 77/00001234 E3") && strlen(msg)<=24); }
    assert(strstr(net_transport_error(60),"(60)"));
    assert(!strcmp(net_transport_error(5),"other"));
    { SslContext c={0}; assert(curl.cb(&curl,&c,NULL)==0 && !g_tls_import_error && g_tls_import_stage==1);
      assert(strstr(net_transport_error(77),"(77)")); }

    pthread_t threads[4];
    for(int i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,thread_test,(void *)(uintptr_t)(0x8000+i)));
    for(int i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
    assert(!g_tls_import_error);

    // ---- sem o subconjunto (falta de memoria na 2a alocacao): E1 continua e a falha e fechada no E1 ----
    reset_ca(); alloc_budget=1; assert(prepare_ca_memory()==0 && g_ca_pem && !g_ca_sub); alloc_budget=-1;
    { SslContext ok={0}; assert(curl.cb(&curl,&ok,NULL)==CURLE_OK && ok.calls==1);
      SslContext bad={.fail=0x77}; assert(curl.cb(&curl,&bad,NULL)==77 && bad.calls==1);
      assert(g_tls_import_stage==1 && !strcmp(net_transport_error(77),"TLS 77/00000077 E1")); }

    reset_ca();
    assert(curl.cb(&curl,&ctx,NULL)==CURLE_OUT_OF_MEMORY);
    assert(prepare_ca_memory()==0); reset_ca();
    puts("TLS CA: bundle real, E1 sem NUL, E2/E3 de fallback, limite de 71, 1000 contextos, 4 threads, falhas de alocacao e verificacao fail-closed passaram (stub do servico SSL).");
}
