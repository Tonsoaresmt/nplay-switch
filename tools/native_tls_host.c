#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int Result;
typedef int SslIoMode;
typedef unsigned u32;
typedef struct {int active;} SslContext;
typedef struct {int active,mode;} SslConnection;
typedef struct AVClass AVClass;
typedef struct AVDictionary AVDictionary;
typedef struct {int listen,verify;const char *host;void *tcp;} TLSShared;
typedef struct {int (*callback)(void *);void *opaque;} AVIOInterruptCB;
typedef struct {void *priv_data;int flags;int64_t rw_timeout;AVIOInterruptCB interrupt_callback;} URLContext;
#define SslVersion_Auto 0
#define SslOptionType_DoNotCloseSocket 1
#define SslOptionType_SkipDefaultVerify 2
#define SslVerifyOption_DateCheck 1u
#define SslVerifyOption_PeerCa 2u
#define SslVerifyOption_HostName 4u
#define SslIoMode_Blocking 0
#define SslIoMode_NonBlocking 1
#define AVIO_FLAG_NONBLOCK 8
#define AV_LOG_ERROR 0
#define true 1
#define R_SUCCEEDED(r) ((r)==0)
#define R_FAILED(r) ((r)!=0)
#define R_VALUE(r) (r)
#define MAKERESULT(m,d) ((m)|((d)<<9))
#define AVERROR(r) (-(r))
#define AVERROR_EXIT (-1414092869)
static int64_t now_us;
static int cancel_flag,cancel_at,checks,handshake_calls,handshake_blocks,read_calls,write_calls;
static int transfer_blocks,transfer_error,transfer_size,configure_error,context_closes,connection_closes;
static int events,trust_error,trust_calls;
static unsigned event_result;
static char event_stage[32];
static int interrupted(void *opaque) {return *(int *)opaque || (cancel_at && now_us>=cancel_at);}
static int ff_check_interrupt(AVIOInterruptCB *cb) {checks++;return cb->callback(cb->opaque);}
static int64_t av_gettime_relative(void) {return now_us;}
static void av_usleep(unsigned n) {assert(n<=10000);now_us+=n;}
static int ff_tls_open_underlying(TLSShared *s,URLContext *h,const char *u,AVDictionary **o) {s->tcp=s;return 0;}
static void ffurl_closep(void **p) {*p=NULL;}
static int ffurl_get_file_handle(void *p) {assert(p);return 1;}
static void av_log(void *p,int level,const char *fmt,...) {}
static Result sslCreateContext(SslContext *s,int version) {s->active=1;return 0;}
static Result sslContextCreateConnection(SslContext *s,SslConnection *c) {assert(s->active);c->active=1;return 0;}
static Result sslConnectionSetOption(SslConnection *c,int o,int b) {assert(c->active);return 0;}
static int socketSslConnectionSetSocketDescriptor(SslConnection *c,int fd) {assert(c->active);return 0;}
static Result sslConnectionSetHostName(SslConnection *c,const char *h,size_t n) {assert(n&&h&&c->active);return 0;}
static Result sslConnectionSetVerifyOption(SslConnection *c,unsigned opt) {
 assert(opt==(SslVerifyOption_DateCheck|SslVerifyOption_PeerCa|SslVerifyOption_HostName));return 0;
}
static Result sslConnectionSetIoMode(SslConnection *c,int mode) {
 assert(mode==SslIoMode_NonBlocking);c->mode=mode;return configure_error;
}
static Result sslConnectionDoHandshake(SslConnection *c,void *a,void *b,void *d,int n) {
 assert(c->mode==SslIoMode_NonBlocking);handshake_calls++;
 return handshake_calls<=handshake_blocks?MAKERESULT(123,204):0;
}
static Result transfer(SslConnection *c,u32 size,u32 *n,int call) {
 assert(c->active&&c->mode==SslIoMode_NonBlocking);
 if(call<=transfer_blocks)return MAKERESULT(123,204);
 if(transfer_error)return transfer_error;
 *n=transfer_size<0?size:(u32)transfer_size;return 0;
}
static Result sslConnectionRead(SslConnection *c,uint8_t *p,u32 size,u32 *n) {return transfer(c,size,n,++read_calls);}
static Result sslConnectionWrite(SslConnection *c,const uint8_t *p,u32 size,u32 *n) {return transfer(c,size,n,++write_calls);}
static void sslConnectionClose(SslConnection *c) {if(c->active){connection_closes++;c->active=0;}}
static void sslContextClose(SslContext *c) {if(c->active){context_closes++;c->active=0;}}
#include "native_tls_callbacks.inc"
#ifndef NPLAY_TEST_NO_TRUST
int nplay_native_tls_configure(void *opaque) {
 SslContext *ctx=opaque;assert(ctx->active);trust_calls++;return trust_error;
}
#endif
void nplay_native_tls_event(const char *stage,unsigned code) {
 events++;event_result=code;snprintf(event_stage,sizeof(event_stage),"%s",stage);
}
static void reset(TLSContext *s,URLContext *h) {
 memset(s,0,sizeof(*s));s->tls_shared.verify=1;s->tls_shared.host="fixture.invalid";
 *h=(URLContext){.priv_data=s,.flags=3,.rw_timeout=35000,.interrupt_callback={interrupted,&cancel_flag}};
 now_us=0;cancel_flag=cancel_at=checks=handshake_calls=read_calls=write_calls=0;
 handshake_blocks=transfer_blocks=transfer_error=configure_error=events=0;transfer_size=-1;
 trust_error=trust_calls=0;
 context_closes=connection_closes=0;
}
int main(void) {
 TLSContext s;URLContext h;uint8_t buf[32];
#ifdef NPLAY_TEST_NO_TRUST
 (void)buf;(void)tls_read;(void)tls_write;
 reset(&s,&h);assert(tls_open(&h,"https://fixture.invalid",3,NULL)==AVERROR(EACCES));
 assert(!handshake_calls&&context_closes==1&&!connection_closes);
 puts("PASS native TLS without CA hook: connection fails closed before handshake.");
 return 0;
#endif
 reset(&s,&h);cancel_flag=1;assert(tls_open(&h,"https://fixture.invalid",3,NULL)==AVERROR_EXIT);
 assert(!handshake_calls&&checks&&context_closes==1&&connection_closes==1);
 reset(&s,&h);handshake_blocks=10000;assert(tls_open(&h,"https://fixture.invalid",3,NULL)==AVERROR(ETIMEDOUT));
 assert(now_us==40000&&handshake_calls==4&&context_closes==1);
 reset(&s,&h);handshake_blocks=10000;cancel_at=20000;
 assert(tls_open(&h,"https://fixture.invalid",3,NULL)==AVERROR_EXIT&&now_us==20000);
 reset(&s,&h);configure_error=MAKERESULT(123,999);
 assert(tls_open(&h,"https://fixture.invalid",3,NULL)==AVERROR(EIO)&&!handshake_calls&&events==1);
 reset(&s,&h);trust_error=77;
 assert(tls_open(&h,"https://fixture.invalid",3,NULL)==AVERROR(EACCES)&&trust_calls==1&&!handshake_calls);
 assert(context_closes==1&&connection_closes==0&&!strcmp(event_stage,"ca-import"));
 for(int i=0;i<1000;i++) {
  reset(&s,&h);handshake_blocks=2;assert(!tls_open(&h,"https://fixture.invalid",3,NULL));
  assert(now_us==20000&&checks>=3&&s.conn.mode==SslIoMode_NonBlocking);
  transfer_blocks=2;assert(tls_read(&h,buf,32)==32&&read_calls==3);
  transfer_size=2;assert(tls_write(&h,buf,32)==2); /* partial write is not discarded */
  transfer_size=0;assert(!tls_read(&h,buf,32)); /* clean EOF */
  cancel_flag=1;assert(tls_read(&h,buf,32)==AVERROR_EXIT);
  tls_close(&h);assert(context_closes==1&&connection_closes==1);
 }
 reset(&s,&h);assert(!tls_open(&h,"https://fixture.invalid",3,NULL));transfer_blocks=10000;
 assert(tls_read(&h,buf,32)==AVERROR(ETIMEDOUT)&&now_us==40000);tls_close(&h);
 reset(&s,&h);assert(!tls_open(&h,"https://fixture.invalid",3,NULL));transfer_blocks=10000;cancel_at=10000;
 assert(tls_write(&h,buf,32)==AVERROR_EXIT&&now_us==10000);tls_close(&h);
 reset(&s,&h);assert(!tls_open(&h,"https://fixture.invalid",3,NULL));h.flags|=AVIO_FLAG_NONBLOCK;transfer_blocks=5;
 assert(tls_read(&h,buf,32)==AVERROR(EAGAIN)&&!now_us);tls_close(&h);
 reset(&s,&h);assert(!tls_open(&h,"https://fixture.invalid",3,NULL));transfer_error=MAKERESULT(123,207);
 assert(tls_read(&h,buf,32)==AVERROR(EACCES)&&events==1&&event_result==(unsigned)transfer_error&&!strcmp(event_stage,"read"));tls_close(&h);
 reset(&s,&h);assert(!tls_open(&h,"https://fixture.invalid",3,NULL));transfer_size=33;
 assert(tls_read(&h,buf,32)==AVERROR(EIO));tls_close(&h);
 reset(&s,&h);assert(!tls_open(&h,"https://fixture.invalid",3,NULL));transfer_size=0;
 assert(tls_write(&h,buf,32)==AVERROR(ETIMEDOUT));tls_close(&h);
 assert(ff_nplay_tls_guard_version()==59);
 puts("PASS patched native TLS callbacks: 1000 lifecycles, nonblocking SDK mode, handshake/read/write deadline+cancel, EOF/partial/nonblock, failed configure, safe error and CA/hostname/date flags. Horizon/network simulated.");
 return 0;
}
