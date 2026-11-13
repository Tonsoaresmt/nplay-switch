#define _POSIX_C_SOURCE 200809L
#include "catalog_fetch.h"
#include "api.h"
#include "net.h"
#include "diag.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SDL_Thread { pthread_t id; int (*fn)(void *); void *arg; int result; };
static pthread_t renderer;
static int created, joined;
int SDL_AtomicGet(SDL_atomic_t *a) { return __atomic_load_n(&a->value,__ATOMIC_SEQ_CST); }
int SDL_AtomicSet(SDL_atomic_t *a,int v) { return __atomic_exchange_n(&a->value,v,__ATOMIC_SEQ_CST); }
Uint32 SDL_GetTicks(void) { return 1; }
static void *thread_entry(void *arg) { SDL_Thread *t=arg;t->result=t->fn(t->arg);return NULL; }
SDL_Thread *SDL_CreateThread(int (*fn)(void *),const char *name,void *arg) {
    assert(!strcmp(name,"catalog-detail"));
    SDL_Thread *t=calloc(1,sizeof(*t));assert(t);t->fn=fn;t->arg=arg;
    assert(!pthread_create(&t->id,NULL,thread_entry,t));created++;return t;
}
void SDL_WaitThread(SDL_Thread *t,int *out) {
    assert(!pthread_join(t->id,NULL));if(out)*out=t->result;free(t);joined++;
}
const char *BASE="https://nplay.test";
char g_token[640];
static long response_code;
static const char *response_body;
static int cancel_in_transport;
static int requests;
long net_request_timeout_cancel(const char *url,const char *method,const char *body,
                               const char *bearer,struct membuf *out,const char **err,
                               long connect,long total,SDL_atomic_t *cancel) {
    assert(!pthread_equal(pthread_self(),renderer));
    assert(!strcmp(url,"https://nplay.test/api/account/profiles"));
    assert(!strcmp(method,"GET") && !body && connect==6 && total==25);
    assert(!bearer || !strcmp(bearer,"fixture-token"));
    requests++;
    if(cancel_in_transport)SDL_AtomicSet(cancel,1);
    *err=response_code<0?"fixture transport failure":NULL;
    if(response_body){out->len=strlen(response_body);out->data=malloc(out->len+1);assert(out->data);memcpy(out->data,response_body,out->len+1);}
    return response_code;
}
void membuf_free(struct membuf *m) { free(m->data);memset(m,0,sizeof(*m)); }
const char *jstr(cJSON *o,const char *k) { cJSON *v=cJSON_GetObjectItemCaseSensitive(o,k);return cJSON_IsString(v)?v->valuestring:NULL; }
void diag_network_event(const char *method,const char *path,long code,unsigned ms,size_t bytes) {
    (void)code;(void)ms;(void)bytes;assert(!strcmp(method,"GET"));
    assert(!strcmp(path,"/api/account/profiles"));
}
#include "login_catalog_worker.inc"

enum {SC_LOADING,SC_LOGIN,SC_SERIES};
enum {FETCH_NONE,FETCH_PROFILES,FETCH_SERIES};
static CatalogFetch g_fetch;
static struct {int kind;char path[512];} g_fetch_current,g_fetch_queued;
static int g_fetch_discard,g_screen,reached_application;
static struct {int active;} g_episode_pending;
static char g_user[128];
static int g_profile_id,g_profile_required,net_profile;
static int token_cleared,user_cleared,profile_cleared;
static char last_toast[192];
static void store_clear_token(void){assert(pthread_equal(pthread_self(),renderer));token_cleared++;}
static void store_clear_user(void){user_cleared++;}
static void store_clear_profile_id(void){profile_cleared++;}
void net_set_profile_id(int n){net_profile=n;}
static void toast(const char *s){snprintf(last_toast,sizeof(last_toast),"%s",s);}
#include "login_clear_session.inc"
#include "login_catalog_dispatch.inc"

static void reset(long code,const char *body) {
    assert(!g_fetch.thread && !g_fetch.result);
    memset(&g_fetch,0,sizeof(g_fetch));memset(&g_fetch_queued,0,sizeof(g_fetch_queued));
    g_fetch_current.kind=FETCH_PROFILES;g_fetch_discard=0;g_screen=SC_LOADING;
    g_episode_pending.active=1;reached_application=0;
    strcpy(g_token,"fixture-token");strcpy(g_user,"Fixture user");
    g_profile_id=net_profile=8;g_profile_required=1;
    token_cleared=user_cleared=profile_cleared=0;last_toast[0]=0;
    response_code=code;response_body=body;cancel_in_transport=0;
}
static void run_fetch(const char *bearer) {
    int was_created=created;
    assert(!catalog_fetch_start(&g_fetch,"/api/account/profiles",bearer));
    // A pump with an unfinished worker never consumes/reuses its state.
    while(!SDL_AtomicGet(&g_fetch.done)) { }
    pump_catalog_fetch();
    assert(created==was_created+1 && !g_fetch.thread);
    for(size_t i=0;i<sizeof(g_fetch.bearer);i++)assert(!g_fetch.bearer[i]);
    if(g_fetch.result){cJSON_Delete(g_fetch.result);g_fetch.result=NULL;}
}
static void preserved(void) {
    assert(g_screen!=SC_LOGIN && !token_cleared && !user_cleared && !profile_cleared);
    assert(!strcmp(g_token,"fixture-token") && !strcmp(g_user,"Fixture user"));
    assert(g_profile_id==8 && net_profile==8);
}
int main(void) {
    renderer=pthread_self();
    for(int i=0;i<100;i++){
        reset(401,"{\"error\":\"Nao autenticado\"}");run_fetch(g_token);
        assert(g_screen==SC_LOGIN && !reached_application);
        assert(token_cleared==1 && user_cleared==1 && profile_cleared==1);
        assert(!g_token[0] && !g_user[0] && !g_profile_id && !net_profile && !g_profile_required);
        assert(!g_episode_pending.active && strstr(last_toast,"sessao"));
        reset(401,"{\"reason\":\"expired\"}");run_fetch(g_token);
        assert(g_screen==SC_LOGIN && strstr(last_toast,"acesso expirou"));
        const long keep[]={-60,-77,-28,-6,-7,-42,403,429,500,502,503,200};
        for(size_t k=0;k<sizeof(keep)/sizeof(keep[0]);k++){
            reset(keep[k],"{\"error\":\"fixture\"}");run_fetch(g_token);preserved();
        }
        reset(401,NULL);run_fetch(g_token);assert(g_screen==SC_LOGIN);
        reset(401,"not-json");run_fetch(g_token);assert(g_screen==SC_LOGIN);
        reset(401,"{\"error\":\"Nao autenticado\"}");run_fetch(NULL);preserved();
        reset(401,"{\"error\":\"Nao autenticado\"}");g_fetch_discard=1;run_fetch(g_token);preserved();
        reset(401,"{\"error\":\"Nao autenticado\"}");g_screen=SC_SERIES;run_fetch(g_token);preserved();
        reset(401,"{\"error\":\"Nao autenticado\"}");cancel_in_transport=1;
        run_fetch(g_token);preserved();
        // A valid queued request supersedes stale 401 and resets its flags.
        reset(401,"{\"error\":\"Nao autenticado\"}");
        assert(!catalog_fetch_start(&g_fetch,"/api/account/profiles",g_token));
        while(!SDL_AtomicGet(&g_fetch.done)) { }
        g_fetch_queued.kind=FETCH_PROFILES;
        strcpy(g_fetch_queued.path,"/api/account/profiles");
        response_code=200;response_body="{\"profiles\":[]}";
        pump_catalog_fetch();assert(g_fetch.thread);
        while(!SDL_AtomicGet(&g_fetch.done)) { }
        pump_catalog_fetch();preserved();
        assert(!g_fetch.authentication_required && !g_fetch.access_expired);
    }
    assert(created==joined && requests==created);
    catalog_fetch_dispose(&g_fetch);
    printf("Login recovery: %d real pthread requests/join; API 401, expired, TLS/network preservation, cancellation and stale/queued results passed.\n",requests);
    return 0;
}
