// Official pinned tls_libnx.c callbacks; Horizon/clock/network are simulated.
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int Result;
typedef int SslIoMode;
typedef unsigned u32;
typedef struct {int value;} SslContext;
typedef struct {int value;} SslConnection;
typedef struct AVClass AVClass;
typedef struct AVDictionary AVDictionary;
typedef struct {int listen,verify; const char *host; void *tcp;} TLSShared;
typedef struct {int (*callback)(void *); void *opaque;} AVIOInterruptCB;
typedef struct {void *priv_data; int flags; int64_t rw_timeout; AVIOInterruptCB interrupt_callback;} URLContext;
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
#define AV_LOG_WARNING 1
#define true 1
#define R_SUCCEEDED(r) ((r)==0)
#define R_FAILED(r) ((r)!=0)
#define R_VALUE(r) (r)
#define MAKERESULT(m,d) ((m)|((d)<<9))
#define AVERROR(r) (-(r))
static int handshake_calls, interrupt_checks, configured_mode, read_calls;
static int64_t simulated_us;
static int ff_check_interrupt(AVIOInterruptCB *cb) {interrupt_checks++;return cb->callback(cb->opaque);}
static int cancelled(void *opaque) {return *(int *)opaque;}
static int ff_tls_open_underlying(TLSShared *s,URLContext *h,const char *u,AVDictionary **o) {return 0;}
static void ffurl_closep(void **p) {*p=NULL;}
static int ffurl_get_file_handle(void *p) {return 1;}
static void av_log(void *p,int level,const char *fmt,...) {}
static void av_usleep(unsigned usec) {simulated_us+=usec;}
static Result sslCreateContext(SslContext *s,int version) {return 0;}
static Result sslContextCreateConnection(SslContext *s,SslConnection *c) {return 0;}
static Result sslConnectionSetOption(SslConnection *s,int opt,int value) {return 0;}
static int socketSslConnectionSetSocketDescriptor(SslConnection *s,int fd) {return 0;}
static Result sslConnectionSetHostName(SslConnection *s,const char *h,size_t n) {return 0;}
static Result sslConnectionSetVerifyOption(SslConnection *s,unsigned opt) {
 assert(opt==(SslVerifyOption_DateCheck|SslVerifyOption_PeerCa|SslVerifyOption_HostName));return 0;
}
static Result sslConnectionSetIoMode(SslConnection *s,int mode) {configured_mode=mode;return 0;}
static Result sslConnectionDoHandshake(SslConnection *s,void *a,void *b,void *c,int n) {
 simulated_us+=10000000;
 return ++handshake_calls<=6?MAKERESULT(123,204):MAKERESULT(123,999);
}
static Result sslConnectionRead(SslConnection *s,uint8_t *b,int size,u32 *out) {
 read_calls++;simulated_us+=50000000;*out=1;return 0;
}
static void sslConnectionClose(SslConnection *s) {}
static void sslContextClose(SslContext *s) {}
#include "torbox_tls_audit.inc"
int main(void) {
 (void)ff_check_interrupt;
 TLSContext state={0};state.tls_shared.verify=1;state.tls_shared.host="fixture.invalid";
 int cancel_requested=1;
 URLContext h={.priv_data=&state,.flags=3,.rw_timeout=30000000,
  .interrupt_callback={cancelled,&cancel_requested}};
 int rc=tls_open(&h,"https://fixture.invalid",3,NULL);
 assert(rc<0 && handshake_calls==7 && simulated_us>h.rw_timeout && !interrupt_checks);
 assert(configured_mode==SslIoMode_Blocking);
 printf("REPRODUCED native TLS handshake: simulated %.2fs with rw_timeout=30s, cancel requested, interrupt checks=%d; forced terminal SDK error ended test.\n",simulated_us/1000000.0,interrupt_checks);
 simulated_us=0;uint8_t buf[16];assert(tls_read(&h,buf,sizeof(buf))==1);
 assert(read_calls==1 && simulated_us>h.rw_timeout && !interrupt_checks);
 printf("REPRODUCED native TLS read: SDK call simulated %.1fs; callback has no own deadline/cancel check.\n",simulated_us/1000000.0);
 puts("Official callback C, injected SDK stalls/time; NOT a Horizon TLS handshake or measured console delay.");
 return 0;
}
