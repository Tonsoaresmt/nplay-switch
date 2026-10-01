#include "net.h"
#include "hot_subtitles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <errno.h>
#define AVERROR(x) (-(x))
#define AVERROR_EOF (-541478725)
#define AVSEEK_SIZE 0x10000
#define JOY_B 1
#define JOY_MINUS 11
const char *BASE="https://nplay.test";
typedef struct { void *cues; int count; } ExternalSubtitleStore;
struct SDL_Thread { int(*run)(void*); void *data; };
static struct SDL_Thread thread;
static int mode,frames,joined,polled,pushed,drawn,calls;
static long response;
static const char *fixture;
int SDL_AtomicSet(SDL_atomic_t *a,int v) { int old=a->value;a->value=v;return old; }
int SDL_AtomicGet(SDL_atomic_t *a) {return a->value;}
Uint32 SDL_GetTicks(void) {return frames*16u;}
SDL_Thread *SDL_CreateThread(int(*f)(void*),const char *name,void *data) {
    assert(!strcmp(name,"hot-subtitle"));
    if(mode==4) return NULL;
    thread.run=f;thread.data=data;return &thread;
}
void SDL_WaitThread(SDL_Thread *t,int *status) {(void)status;assert(t==&thread&&frames==4);joined++;}
void SDL_Delay(Uint32 ms) {assert(ms==16);frames++;if(frames==4)thread.run(thread.data);}
int SDL_PollEvent(SDL_Event *e) {
    if(frames!=2||polled||mode==0||mode==4||mode==5)return 0;
    polled=1;memset(e,0,sizeof(*e));
    if(mode==1){e->type=SDL_JOYBUTTONDOWN;e->jbutton.button=JOY_B;}
    if(mode==2){e->type=SDL_FINGERDOWN;e->tfinger.y=0.95f;}
    if(mode==3)e->type=SDL_QUIT;
    return 1;
}
int SDL_PushEvent(SDL_Event *e) {assert(e->type==SDL_QUIT);pushed++;return 1;}
void SDL_RenderPresent(SDL_Renderer *r) {(void)r;drawn++;}
void pui_draw_loading(SDL_Renderer *r,const char *t,const char *h,const char *d,Uint32 tick,int cancellable) {
    (void)r;(void)t;(void)h;(void)d;(void)tick;assert(cancellable);
}
static void external_subtitle_clear(ExternalSubtitleStore *s) {free(s->cues);memset(s,0,sizeof(*s));}
static int load_external_subtitle(const char *url,ExternalSubtitleStore *s,int direct,SDL_atomic_t *cancel) {
    assert(!strcmp(url,"owned-until-join")&&direct==1&&cancel);calls++;
    if(mode==5)return -1;
    /* Complete despite cancellation to test rejection of a late success. */
    s->cues=malloc(16);s->count=2;return 0;
}
void membuf_free(struct membuf *b) {free(b->data);memset(b,0,sizeof(*b));}
long net_get_text_limited(const char *url,size_t limit,long timeout,SDL_atomic_t *cancel,struct membuf *out) {
    assert(strstr(url,"/probe")&&limit==256*1024&&timeout==5000&&cancel);calls++;
    if(response==200){out->len=strlen(fixture);out->data=malloc(out->len+1);memcpy(out->data,fixture,out->len+1);}
    return response;
}
#include "subtitle_io_test.inc"
static void reset(int scenario) {mode=scenario;frames=joined=polled=pushed=drawn=calls=0;}
int main(void) {
    for(int i=0;i<=5;i++) {
        reset(i); HotSubtitleJob job={.url="owned-until-join"};
        int rc=hot_subtitle_wait(NULL,"title",&job);
        assert((rc==0)==(i==0));assert(joined==(i==4?0:1));
        if(i==0) assert(job.loaded.count==2);
        else assert(job.loaded.count==0);
        assert(pushed==(i==3));assert(drawn==(i==4?0:4));
        external_subtitle_clear(&job.loaded);
    }
    fixture="{\"ok\":true,\"subtitles\":[{\"index\":2,\"language\":\"por\"}]}";
    const long codes[]={200,404,503};
    for(int i=0;i<3;i++) {
        reset(0);response=codes[i];HlsManifestTrack tracks[16];
        HotSubtitleJob job={.sid="1234567890abcdef",.tracks=tracks};
        int rc=hot_subtitle_wait(NULL,"title",&job);
        assert(rc==(i==0?1:-1));assert(joined==1&&calls==1);
    }
    struct membuf buffer={0};SDL_atomic_t cancel={0};LimitedText ctx={&buffer,4096,&cancel};
    char bytes[4096];memset(bytes,'x',sizeof(bytes));
    assert(limited_text_write(bytes,1,4095,&ctx)==4095);
    assert(limited_text_write(bytes,1,1,&ctx)==1);
    assert(buffer.len==4096&&buffer.cap==4097&&buffer.data[4096]==0);
    assert(limited_text_write(bytes,1,1,&ctx)==0&&buffer.len==4096);
    assert(limited_text_write(bytes,SIZE_MAX,2,&ctx)==0);
    membuf_free(&buffer);cancel.value=1;
    assert(limited_text_write(bytes,1,1,&ctx)==0&&!buffer.data);
    SubtitleMemory memory={(const unsigned char *)"abcdef",6,0};unsigned char out[8];
    assert(subtitle_memory_read(&memory,out,3)==3&&!memcmp(out,"abc",3));
    assert(subtitle_memory_seek(&memory,0,AVSEEK_SIZE)==6);
    assert(subtitle_memory_seek(&memory,-2,SEEK_END)==4);
    assert(subtitle_memory_read(&memory,out,8)==2&&!memcmp(out,"ef",2));
    assert(subtitle_memory_read(&memory,out,8)==AVERROR_EOF);
    assert(subtitle_memory_seek(&memory,INT64_MAX,SEEK_CUR)<0);
    assert(subtitle_memory_seek(&memory,INT64_MIN,SEEK_CUR)<0);
    assert(subtitle_memory_seek(&memory,0,SEEK_SET)==0);
    puts("SUBTITLE IO: actual worker wait/join, 404/503, late success, B/touch/quit, preserved track, bounded bytes and memory seek passed");
}
