#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include "cover_policy.h"
typedef uint32_t Uint32;
typedef struct { int w,h,pitch; } SDL_Surface;
typedef struct { size_t bytes; } SDL_Texture;
typedef int SDL_mutex;
typedef int SDL_sem;
typedef struct { int value; } SDL_atomic_t;
typedef struct { int unused; } SDL_RWops;
#define SDL_PIXELFORMAT_RGBA32 1
#define SDL_BLENDMODE_NONE 0
static void *gRen;
static const char *BASE="https://example.invalid";
static Uint32 tick=100;
static int requests,image_w=1920,image_h=1080;
static int surfaces,textures,fail_texture,fail_scale,suspend_in_request;
static long status=200;
static char request_url[COVER_URL_MAX+256];
static Uint32 SDL_GetTicks(void){return tick;}
static void SDL_LockMutex(SDL_mutex *m){(void)m;}
static void SDL_UnlockMutex(SDL_mutex *m){(void)m;}
static int SDL_AtomicGet(SDL_atomic_t *a){return a->value;}
static int SDL_AtomicSet(SDL_atomic_t *a,int v){int old=a->value;a->value=v;return old;}
static int SDL_SemWaitTimeout(SDL_sem *s,int ms);
static void SDL_SemPost(SDL_sem *s){(void)s;}
static SDL_Surface *surface(int w,int h){SDL_Surface *s=malloc(sizeof(*s));assert(s);*s=(SDL_Surface){w,h,w*4};surfaces++;return s;}
static void SDL_FreeSurface(SDL_Surface *s){assert(s);surfaces--;free(s);}
static SDL_Surface *SDL_CreateRGBSurfaceWithFormat(int a,int w,int h,int b,int c){(void)a;(void)b;(void)c;return fail_scale?NULL:surface(w,h);}
static int SDL_SetSurfaceBlendMode(SDL_Surface *s,int m){(void)s;(void)m;return 0;}
static int SDL_BlitScaled(SDL_Surface *s,void *a,SDL_Surface *d,void *b){(void)s;(void)a;(void)d;(void)b;return 0;}
static size_t live_texture_bytes;
static SDL_Texture *SDL_CreateTextureFromSurface(void *r,SDL_Surface *s){
    (void)r;if(fail_texture)return NULL;
    size_t bytes=(size_t)s->w*s->h*4;
    assert(live_texture_bytes+bytes<=COVER_TEXTURE_BYTES_MAX);
    SDL_Texture *t=malloc(sizeof(*t));assert(t);t->bytes=bytes;textures++;live_texture_bytes+=bytes;return t;
}
static void SDL_DestroyTexture(SDL_Texture *t){assert(t);textures--;live_texture_bytes-=t->bytes;free(t);}
static SDL_RWops *SDL_RWFromMem(void *p,int n){(void)p;(void)n;static SDL_RWops rw;return &rw;}
static SDL_Surface *IMG_Load_RW(SDL_RWops *rw,int release){(void)rw;(void)release;return surface(image_w,image_h);}
#include "app_membuf.inc"
static void membuf_free(struct membuf *m){free(m->data);m->data=NULL;m->len=m->cap=0;}
static long net_request_timeout_cancel(const char *,const char *,const char *,const char *,struct membuf *,const char **,long,long,SDL_atomic_t *);
static int fail_url_alloc;
static void *cover_malloc(size_t size){return fail_url_alloc?NULL:malloc(size);}
#define malloc cover_malloc
#include "app_cover.inc"
#undef malloc
#include "app_cover_suspend.inc"
#include "app_net_write.inc"
static int SDL_SemWaitTimeout(SDL_sem *s,int ms){(void)s;(void)ms;if(g_qn)return 0;g_run=0;return 1;}
static long net_request_timeout_cancel(const char *url,const char *method,const char *body,const char *token,struct membuf *m,const char **err,long connect,long total,SDL_atomic_t *cancel){
    (void)method;(void)body;(void)token;(void)err;assert(connect==6&&total==15);
    assert(cancel==&g_cover_suspended && m->limit==COVER_DOWNLOAD_MAX);
    snprintf(request_url,sizeof(request_url),"%s",url);requests++;
    if(suspend_in_request)SDL_AtomicSet(cancel,1);
    m->data=malloc(64);assert(m->data);memset(m->data,'x',64);m->len=m->cap=64;
    return status;
}
static void work(void){g_run=1;cover_worker(NULL);}
static void reset(void){
    cover_suspend_and_release();
    for(int i=0;i<g_covN;i++)free(g_cov[i].url);
    memset(g_cov,0,sizeof(g_cov));memset(g_cov_hash,0,sizeof(g_cov_hash));
    g_covN=0;g_cov_url_bytes=0;cover_resume_after_playback();g_cover_frame=1;
    fail_texture=fail_scale=suspend_in_request=0;status=200;
    assert(!surfaces&&!textures);
}
int main(void){
    g_cover_frame=1;
    char url[1500];memset(url,'a',sizeof(url));memcpy(url,"https://example.invalid/",24);url[1499]=0;
    cover_get(url);cover_get(url);assert(g_covN==1&&g_qn==1);
    work();assert(!strcmp(request_url,url));cover_pump();assert(cover_get(url));
    puts("PASS extracted covers: 1499-byte URL retained and deduplicated, not truncated");
    reset();fail_url_alloc=1;cover_get("/no-memory");assert(!g_covN&&!g_qn&&!g_cov_url_bytes);
    fail_url_alloc=0;
    char oversized[COVER_URL_MAX+1];memset(oversized,'x',sizeof(oversized));oversized[COVER_URL_MAX]=0;
    cover_get(oversized);assert(!g_covN);
    for(int i=0;i<2200;i++){
        memset(url,'a',sizeof(url));snprintf(url,40,"https://example.invalid/%06d",i);
        size_t prefix=strlen(url);memset(url+prefix,'a',sizeof(url)-prefix-1);url[1499]=0;
        cover_get(url);assert(g_cov_url_bytes<=COVER_URL_BYTES_MAX);
    }
    assert(g_covN<MAX_COV&&g_covN>2000);reset();
    puts("PASS extracted covers: URL allocation failure/oversize rejected, metadata obeys 3MiB budget");
    reset();status=503;cover_get("/retry");work();assert(g_cov[0].state==3);
    int old=requests;cover_get("/retry");assert(!g_qn&&requests==old);
    tick+=1999;cover_get("/retry");assert(!g_qn);
    tick++;cover_get("/retry");assert(g_qn==1);work();assert(g_cov[0].retry_at==tick+5000);
    tick+=5000;status=200;cover_get("/retry");work();cover_pump();assert(cover_get("/retry")&&g_cov[0].failures==0);
    assert(cover_retry_delay(4,503)==60000&&cover_retry_delay(1,404)==300000);
    puts("PASS extracted covers: transient retry/backoff, success reset, dead-image cooldown");
    reset();cover_get("/old");tick+=1501;old=requests;work();assert(requests==old&&g_cov[0].state==0);
    cover_get("/visible");work();cover_pump();assert(cover_get("/visible"));
    puts("PASS extracted covers: stale off-screen queue skipped, current viewport loads");
    reset();char id[64];image_w=1920;image_h=1080;
    for(int i=0;i<30;i++){snprintf(id,sizeof(id),"/budget%d",i);cover_get(id);}
    work();assert(g_rn<=COVER_READY_MAX&&g_cov_ready_bytes<=COVER_READY_BYTES_MAX);
    assert(surfaces==g_rn&&surfaces<30);
    while(g_rn){cover_pump();}
    assert(!surfaces&&g_cov_ready_bytes==0);
    for(int i=30;i<100;i++){snprintf(id,sizeof(id),"/budget%d",i);cover_get(id);work();cover_pump();}
    assert(g_cov_texture_bytes<=COVER_TEXTURE_BYTES_MAX&&textures==g_cov_texN);
    assert(g_cov_texN<100&&g_cov_texN<=MAX_COVER_TEXTURES);
    puts("PASS extracted covers: 30-result burst bounded, 100 backdrops obey byte/count budgets");
    reset();suspend_in_request=1;cover_get("/cancel");work();assert(!g_rn&&!surfaces&&g_cov[0].state==0);
    cover_resume_after_playback();suspend_in_request=0;cover_get("/cancel");work();cover_pump();assert(cover_get("/cancel"));
    reset();fail_scale=1;cover_get("/scale-fail");work();assert(!surfaces&&!g_rn&&g_cov[0].state==3);
    reset();fail_texture=1;cover_get("/texture-fail");work();cover_pump();assert(!surfaces&&!textures&&g_cov[0].state==3);
    puts("PASS extracted covers: suspension, resumption and allocation failure cleanup");
    reset();image_w=100;image_h=150;
    for(int i=0;i<MAX_COV;i++){
        snprintf(id,sizeof(id),"/metadata%d",i);cover_get(id);work();cover_pump();
    }
    assert(g_covN==MAX_COV);
    int pinned=cover_find_locked("/metadata0",0);
    cover_get("/metadata0");work();cover_pump();cover_get("/metadata0");
    SDL_Texture *borrowed=g_cov[pinned].tex;assert(borrowed);
    cover_get("/metadata-new");assert(cover_find_locked("/metadata0",0)==pinned&&g_cov[pinned].tex==borrowed);
    assert(g_cov_url_bytes<=COVER_URL_BYTES_MAX);work();cover_pump();
    puts("PASS extracted covers: 3000-entry recycle preserves texture borrowed by current frame");
    reset();struct membuf buf={.limit=5};
    assert(write_cb("abc",1,3,&buf)==3);assert(write_cb("def",1,3,&buf)==0);
    assert(buf.len==3&&buf.cap<=6&&!strcmp(buf.data,"abc"));
    assert(write_cb("de",1,2,&buf)==2&&buf.len==5);
    assert(write_cb("!",1,1,&buf)==0);membuf_free(&buf);
    puts("PASS actual network writer: streaming byte cap independent of Content-Length");
    int w,h;assert(cover_surface_size(512,512,&w,&h)&&w==384&&h==384);
    assert(cover_surface_size(200,300,&w,&h)&&w==200&&h==300);
    assert(cover_surface_size(2000,3000,&w,&h)&&w==512&&h==768);
    assert(cover_surface_size(1920,1080,&w,&h)&&w==1280&&h==720);
    assert(!cover_surface_size(0,100,&w,&h));
    puts("APP COVER PASS (mocked SDL/HTTP; not actual network, GPU, or concurrent scheduler)");
}
