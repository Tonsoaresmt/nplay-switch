#include "net.h"
#include "subtitle_limits.h"
#include <curl/curl.h>
#include <stdarg.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef curl_easy_setopt
#undef curl_easy_getinfo
#define USER_AGENT "test"
static int handle, opened, cleaned, verified_peer, verified_host, follow, cancel_during;
static size_t body_size, delivered;
static long status = 200;
static SDL_atomic_t *cancel_ptr;
static size_t (*write_cb)(char *, size_t, size_t, void *);
static void *write_data;
static int (*progress_cb)(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t);
static void *progress_data;
static char chunk[65536];
int SDL_AtomicGet(SDL_atomic_t *v) { return v->value; }
Uint32 SDL_GetTicks(void) { return 100; }
void membuf_free(struct membuf *b) { free(b->data); memset(b, 0, sizeof(*b)); }
void net_configure_curl_isolated(CURL *c) { assert(c == (CURL *)&handle); }
static int request_cancel_cb(void *p, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)a; (void)b; (void)c; (void)d; return SDL_AtomicGet(p);
}
CURL *curl_easy_init(void) {
    opened++; verified_peer=verified_host=0; follow=-1;
    write_cb=NULL; progress_cb=NULL; progress_data=NULL; delivered=0;
    return (CURL *)&handle;
}
CURLcode curl_easy_setopt(CURL *c, CURLoption opt, ...) {
    assert(c == (CURL *)&handle);
    va_list args; va_start(args, opt);
    switch (opt) {
    case CURLOPT_SSL_VERIFYPEER: verified_peer=(int)va_arg(args,long); break;
    case CURLOPT_SSL_VERIFYHOST: verified_host=(int)va_arg(args,long); break;
    case CURLOPT_FOLLOWLOCATION: follow=(int)va_arg(args,long); break;
    case CURLOPT_WRITEFUNCTION: write_cb=va_arg(args,size_t (*)(char *,size_t,size_t,void *)); break;
    case CURLOPT_WRITEDATA: write_data=va_arg(args,void *); break;
    case CURLOPT_XFERINFOFUNCTION: progress_cb=va_arg(args,int (*)(void *,curl_off_t,curl_off_t,curl_off_t,curl_off_t)); break;
    case CURLOPT_XFERINFODATA: progress_data=va_arg(args,void *); break;
    default: break;
    }
    va_end(args); return CURLE_OK;
}
CURLcode curl_easy_getinfo(CURL *c, CURLINFO info, ...) {
    assert(c == (CURL *)&handle && info == CURLINFO_RESPONSE_CODE);
    va_list args; va_start(args, info); *va_arg(args,long *)=status; va_end(args); return CURLE_OK;
}
CURLcode curl_easy_perform(CURL *c) {
    assert(c == (CURL *)&handle && verified_peer==1 && verified_host==2 && follow==0 && write_cb);
    // No Content-Length: exercise the callback's independent byte ceiling.
    while (delivered < body_size) {
        if (cancel_during && delivered >= 65536) cancel_ptr->value=1;
        if (progress_cb && progress_cb(progress_data,0,0,0,0)) return CURLE_ABORTED_BY_CALLBACK;
        size_t n=body_size-delivered; if(n>sizeof(chunk)) n=sizeof(chunk);
        if (write_cb(chunk,1,n,write_data)!=n) return CURLE_WRITE_ERROR;
        delivered+=n;
    }
    return CURLE_OK;
}
void curl_easy_cleanup(CURL *c) { assert(c==(CURL *)&handle); cleaned++; }
#include "subtitle_transport.inc"
static int count_chunk(const char *data, size_t length, void *opaque) {
    assert(data); *(size_t *)opaque+=length; return 1;
}
int main(void) {
    memset(chunk,'x',sizeof(chunk));
    SDL_atomic_t cancel={0}; cancel_ptr=&cancel;
    for (int stream=0;stream<2;stream++) {
        for (int i=0;i<3;i++) {
            body_size=i==0?5u*1024u*1024u:i==1?SUBTITLE_DOWNLOAD_MAX:SUBTITLE_DOWNLOAD_MAX+1;
            struct membuf body={0}; size_t counted=0;
            long code=stream?net_stream_text("https://nplay.test/vtt",SUBTITLE_DOWNLOAD_MAX,&cancel,count_chunk,&counted):
                net_get_text_limited("https://nplay.test/vtt",SUBTITLE_DOWNLOAD_MAX,15000,&cancel,&body);
            assert((code==200)==(i<2));
            if(stream) assert(counted==(i<2?body_size:SUBTITLE_DOWNLOAD_MAX));
            else if(i<2) assert(body.len==body_size&&body.data[body.len]==0&&body.cap<=SUBTITLE_DOWNLOAD_MAX+1);
            else assert(!body.data);
            membuf_free(&body);
        }
        body_size=200; status=503; size_t counted=0; struct membuf body={0};
        long code=stream?net_stream_text("https://nplay.test/vtt",SUBTITLE_DOWNLOAD_MAX,&cancel,count_chunk,&counted):
            net_get_text_limited("https://nplay.test/vtt",SUBTITLE_DOWNLOAD_MAX,15000,&cancel,&body);
        assert(code==503&&!body.data&&!counted);
        // Error responses must obey the byte ceiling too.
        body_size=SUBTITLE_DOWNLOAD_MAX+1;
        code=stream?net_stream_text("https://nplay.test/vtt",SUBTITLE_DOWNLOAD_MAX,&cancel,count_chunk,&counted):
            net_get_text_limited("https://nplay.test/vtt",SUBTITLE_DOWNLOAD_MAX,15000,&cancel,&body);
        assert(code<0&&!body.data&&!counted&&delivered==SUBTITLE_DOWNLOAD_MAX);
        status=200; body_size=131072; cancel_during=1;
        code=stream?net_stream_text("https://nplay.test/vtt",SUBTITLE_DOWNLOAD_MAX,&cancel,count_chunk,&counted):
            net_get_text_limited("https://nplay.test/vtt",SUBTITLE_DOWNLOAD_MAX,15000,&cancel,&body);
        assert(code<0&&!body.data); cancel.value=0; cancel_during=0;
    }
    int before=opened; struct membuf body={0}; size_t counted=0;
    assert(net_get_text_limited("https://nplay.test",SUBTITLE_DOWNLOAD_MAX+1,1000,&cancel,&body)<0);
    assert(net_stream_text("https://nplay.test",SUBTITLE_DOWNLOAD_MAX+1,&cancel,count_chunk,&counted)<0);
    assert(net_get_text_limited("http://nplay.test",100,1000,&cancel,&body)<0);
    assert(net_stream_text("http://nplay.test",100,&cancel,count_chunk,&counted)<0);
    assert(opened==before&&opened==cleaned);
    puts("SUBTITLE TRANSPORT OK: real HTTPS entry points, 5/8 MiB accepted, oversized chunked/error bodies rejected, TLS verified, cancellation, cleanup");
    return 0;
}
