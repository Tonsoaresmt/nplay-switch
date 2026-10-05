#include <SDL.h>
#include "net.h"
#include "subtitle_store.h"
#include "subtitle_limits.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define AVERROR(e) (-(e))
#define AVERROR_EOF (-541478725)
#define AVERROR_EXIT (-1414092869)
#define AVERROR_PROTOCOL_NOT_FOUND (-1330794744)
#define AV_NOPTS_VALUE INT64_MIN
#define AV_TIME_BASE 1000000
#define AVFMT_FLAG_CUSTOM_IO 1
#define AVIO_FLAG_WRITE 2
#define AVSEEK_SIZE 0x10000
#define AVMEDIA_TYPE_SUBTITLE 3
#define AV_CODEC_ID_NONE 0
#define AV_CODEC_ID_WEBVTT 1
#define SUBTITLE_ASS 1
#define SUBTITLE_TEXT 2
typedef struct {int num,den;} AVRational;
typedef struct {unsigned char *buffer;int error;} AVIOContext;
typedef struct {int codec_id;} AVCodecParameters;
typedef struct {AVCodecParameters *codecpar;AVRational time_base;} AVStream;
typedef struct {int unused;} AVDictionary;
typedef struct {int unused;} AVInputFormat;
typedef struct {int unused;} AVCodec;
typedef struct {int unused;} AVCodecContext;
typedef struct {int stream_index;int64_t pts,duration;} AVPacket;
typedef struct {int type;char *text,*ass;} AVSubtitleRect;
typedef struct {int64_t pts;unsigned start_display_time,end_display_time,num_rects;AVSubtitleRect **rects;} AVSubtitle;
typedef struct AVFormatContext {
    AVIOContext *pb;int flags;
    int (*io_open)(struct AVFormatContext *,AVIOContext **,const char *,int,AVDictionary **);
    int (*io_close2)(struct AVFormatContext *,AVIOContext *);
    struct {int (*callback)(void *);void *opaque;} interrupt_callback;
    AVStream **streams;
} AVFormatContext;
typedef struct {SubtitleStore cues;} ExternalSubtitleStore;
static int reads,terminal,cancel_after,skip_fail,decode_fail,add_fail,resource_fail;
static int io_active,formats,packets,codecs,failures,cases;
static long http_code;
static AVStream stream={NULL,{1,1000}};
static AVStream *streams[]={&stream};
int SDL_AtomicGet(SDL_atomic_t *a) {return a->value;}
static void diag_player_event(const char *area,const char *event,const char *fmt,...) {
    (void)area;(void)event;(void)fmt;
}
static void external_subtitle_clear(ExternalSubtitleStore *s) {subtitle_store_free(&s->cues);}
static int external_subtitle_count(const ExternalSubtitleStore *s) {return subtitle_store_count(&s->cues);}
static int external_subtitle_add(ExternalSubtitleStore *s,double a,double b,const char *t,const SubtitlePlacement *at) {
    if(add_fail&&reads>1)return 0;
    return subtitle_store_add_at(&s->cues,a,b,t,at);
}
static void *av_malloc(size_t n) {return malloc(n);}
static void av_free(void *p) {free(p);}
static void av_freep(void *p) {void **pp=p;free(*pp);*pp=NULL;}
static AVIOContext *avio_alloc_context(unsigned char *b,int size,int write,void *data,
    int (*read)(void*,unsigned char*,int),void *unused,int64_t (*seek)(void*,int64_t,int)) {
    (void)size;(void)write;(void)data;(void)read;(void)unused;(void)seek;
    AVIOContext *c=calloc(1,sizeof(*c));assert(c);c->buffer=b;io_active++;return c;
}
static void avio_context_free(AVIOContext **p) {if(*p){free(*p);*p=NULL;io_active--;}}
static AVIOContext *nplay_curl_avio_open_hls(const char *url) {
    if(resource_fail&&strstr(url,"segment"))return NULL;
    return avio_alloc_context(NULL,0,0,NULL,NULL,NULL,NULL);
}
static void nplay_curl_avio_close(AVIOContext *c) {if(c){free(c->buffer);avio_context_free(&c);}}
static AVFormatContext *avformat_alloc_context(void) {
    AVFormatContext *f=calloc(1,sizeof(*f));assert(f);f->streams=streams;formats++;return f;
}
static void avformat_free_context(AVFormatContext *f) {if(f){free(f);formats--;}}
static void avformat_close_input(AVFormatContext **f) {avformat_free_context(*f);*f=NULL;}
static const AVInputFormat *av_find_input_format(const char *name) {(void)name;static AVInputFormat f;return &f;}
static int av_dict_set(AVDictionary **d,const char *k,const char *v,int flags) {(void)d;(void)k;(void)v;(void)flags;return 0;}
static void av_dict_free(AVDictionary **d) {(void)d;}
static int avformat_open_input(AVFormatContext **f,const char *url,const AVInputFormat *fmt,AVDictionary **d) {
    (void)f;(void)url;(void)fmt;(void)d;return 0;
}
static int avformat_find_stream_info(AVFormatContext *f,void *o) {(void)f;(void)o;return 0;}
static int av_find_best_stream(AVFormatContext *f,int type,int a,int b,void *c,int flags) {
    (void)f;(void)type;(void)a;(void)b;(void)c;(void)flags;return 0;
}
static const AVCodec *avcodec_find_decoder(int id) {(void)id;static AVCodec c;return &c;}
static AVCodecContext *avcodec_alloc_context3(const AVCodec *c) {(void)c;codecs++;return calloc(1,sizeof(AVCodecContext));}
static AVPacket *av_packet_alloc(void) {packets++;return calloc(1,sizeof(AVPacket));}
static void av_packet_free(AVPacket **p) {if(*p){free(*p);*p=NULL;packets--;}}
static void av_packet_unref(AVPacket *p) {(void)p;}
static void avcodec_free_context(AVCodecContext **p) {if(*p){free(*p);*p=NULL;codecs--;}}
static int avcodec_parameters_to_context(AVCodecContext *c,AVCodecParameters *p) {(void)c;(void)p;return 0;}
static int avcodec_open2(AVCodecContext *c,const AVCodec *d,void *o) {(void)c;(void)d;(void)o;return 0;}
static int av_read_frame(AVFormatContext *f,AVPacket *p) {
    if(reads==2){
        if(skip_fail&&f->io_open){
            AVIOContext *child=NULL;resource_fail=skip_fail==1;
            if(skip_fail==1) assert(f->io_open(f,&child,"https://test/segment.vtt",0,NULL)<0&&!child);
            else {
                assert(f->io_open(f,&child,"https://test/segment.vtt",0,NULL)==0&&child);
                child->error=skip_fail==2?AVERROR(EIO):AVERROR_EOF;
                assert(f->io_close2(f,child)==0);
            }
        }
        return terminal;
    }
    reads++;p->stream_index=0;p->pts=reads*10000;p->duration=2000;
    if(cancel_after&&reads==2)((SDL_atomic_t*)f->interrupt_callback.opaque)->value=1;
    return 0;
}
static int avcodec_decode_subtitle2(AVCodecContext *c,AVSubtitle *s,int *got,AVPacket *p) {
    (void)c;(void)p;
    if(decode_fail&&reads==2)return AVERROR(EINVAL);
    static AVSubtitleRect r={SUBTITLE_TEXT,"new dialogue",NULL};static AVSubtitleRect *rr[]={&r};
    s->pts=AV_NOPTS_VALUE;s->num_rects=1;s->rects=rr;*got=1;return 0;
}
static void avsubtitle_free(AVSubtitle *s) {(void)s;}
static double av_q2d(AVRational q) {return (double)q.num/q.den;}
static void ass_to_text(const char *s,char *out,size_t cap) {snprintf(out,cap,"%s",s);}
static void packet_subtitle_placement(const AVPacket *p,SubtitlePlacement *at) {(void)p;memset(at,0,sizeof(*at));}
void membuf_free(struct membuf *b) {free(b->data);memset(b,0,sizeof(*b));}
long net_get_text_limited(const char *url,size_t cap,long timeout,SDL_atomic_t *cancel,struct membuf *b) {
    (void)url;(void)cancel;assert(cap==SUBTITLE_DOWNLOAD_MAX&&timeout==15000);
    if(http_code==200){b->len=8;b->data=malloc(9);memcpy(b->data,"WEBVTT\n\n",9);}return http_code;
}
/* Baseline callbacks, unused when the corrected loader uses isolated ones. */
static int player_hls_io_open(AVFormatContext *f,AVIOContext **pb,const char *url,int flags,AVDictionary **d);
static int player_hls_io_close(AVFormatContext *f,AVIOContext *pb);
#include "subtitle_completion.inc"
static int player_hls_io_open(AVFormatContext *f,AVIOContext **pb,const char *url,int flags,AVDictionary **d) {
    (void)f;(void)flags;(void)d;*pb=nplay_curl_avio_open_hls(url);return *pb?0:AVERROR(ENOMEM);
}
static int player_hls_io_close(AVFormatContext *f,AVIOContext *pb) {(void)f;nplay_curl_avio_close(pb);return 0;}
static void check(int direct,int ending,int cancelled,int skip,int decode,int cap,int expected,const char *name) {
    cases++;reads=0;terminal=ending;cancel_after=cancelled;skip_fail=skip;decode_fail=decode;add_fail=cap;resource_fail=0;
    SDL_atomic_t cancel={0};ExternalSubtitleStore s={0};
    assert(subtitle_store_add(&s.cues,100,102,"old track"));
    int rc=load_external_subtitle_data("https://test/sub.m3u8",&s,direct,&cancel,NULL,0);
    int ok=(rc==0)==expected;
    if(expected){ok&=external_subtitle_count(&s)==2&&!strcmp(subtitle_store_text(&s.cues,10.5),"new dialogue");}
    else {ok&=external_subtitle_count(&s)==1&&!strcmp(subtitle_store_text(&s.cues,100.5),"old track");}
    printf("%s %s rc=%d cues=%d\n",ok?"PASS":"FAIL",name,rc,external_subtitle_count(&s));
    failures+=!ok;external_subtitle_clear(&s);
    assert(io_active==0&&formats==0&&codecs==0&&packets==0);
}
int main(void) {
    AVCodecParameters par={AV_CODEC_ID_NONE};stream.codecpar=&par;http_code=200;
    /* Keep baseline helper declarations warning-clean without calling them. */
    (void)player_hls_io_open;(void)player_hls_io_close;
    for(int direct=0;direct<=1;direct++) {
        check(direct,AVERROR_EOF,0,0,0,0,1,direct?"VTT complete":"HLS complete");
        check(direct,AVERROR(EIO),0,0,0,0,0,"partial network error preserves old track");
        check(direct,AVERROR(EAGAIN),0,0,0,0,0,"partial temporarily unavailable");
        check(direct,AVERROR_EXIT,0,0,0,0,0,"partial interrupted");
        check(direct,AVERROR_EOF,1,0,0,0,0,"late cancellation");
        check(direct,AVERROR_EOF,0,0,0,1,0,"decoded store ceiling");
        check(direct,AVERROR_EOF,0,0,1,0,0,"decoder drops later dialogue");
    }
    check(0,AVERROR_EOF,0,1,0,0,0,"skipped segment followed by EOF");
    check(0,AVERROR_EOF,0,2,0,0,0,"nested AVIO read failure followed by EOF");
    check(0,AVERROR_EOF,0,3,0,0,1,"nested normal EOF is not a failure");
    check(0,AVERROR_EOF,0,0,0,0,1,"retry after failed segment resets failure state");
    http_code=503;check(1,AVERROR_EOF,0,0,0,0,0,"HTTP 503 preserves old track");
    printf("SUBTITLE COMPLETION: %d cases, %d failures; real loader/store, simulated demux/network.\n",cases,failures);
    return failures?1:0;
}
