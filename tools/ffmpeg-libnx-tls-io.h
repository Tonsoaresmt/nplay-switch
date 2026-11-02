/* Nplay native FFmpeg/libnx TLS: bounded nonblocking IPC, verified TLS unchanged.
 * Included once by tls_libnx.c, after TLSContext. No renderer or socket ownership.
 */
#ifndef NPLAY_NATIVE_TLS_IO_H
#define NPLAY_NATIVE_TLS_IO_H
extern void nplay_native_tls_event(const char *stage, unsigned result) __attribute__((weak));
extern int nplay_native_tls_configure(void *ssl_context) __attribute__((weak));

static int nplay_tls_trust(TLSContext *ctx) {
    if (!nplay_native_tls_configure || nplay_native_tls_configure(&ctx->context)) {
        if (nplay_native_tls_event) nplay_native_tls_event("ca-import", 77u);
        return AVERROR(EACCES);
    }
    return 0;
}

/* Release validator checks this symbol in the linked vendor library and ELF. */
int ff_nplay_tls_guard_version(void);
int ff_nplay_tls_guard_version(void) { return 59; }

static int nplay_tls_failure(const char *stage, Result rc) {
    if (nplay_native_tls_event) nplay_native_tls_event(stage, (unsigned)R_VALUE(rc));
    return R_VALUE(rc) == MAKERESULT(123, 207) ? AVERROR(EACCES) : AVERROR(EIO);
}

static int nplay_tls_check(URLContext *h, int64_t started) {
    int64_t timeout = h->rw_timeout > 0 ? h->rw_timeout : 30000000LL;
    if (ff_check_interrupt(&h->interrupt_callback)) return AVERROR_EXIT;
    if (av_gettime_relative() - started >= timeout) return AVERROR(ETIMEDOUT);
    return 0;
}

static int nplay_tls_handshake(URLContext *h) {
    TLSContext *ctx = h->priv_data;
    int64_t started = av_gettime_relative();
    for (;;) {
        Result rc;
        int err = nplay_tls_check(h, started);
        if (err) return err;
        rc = sslConnectionDoHandshake(&ctx->conn, NULL, NULL, NULL, 0);
        if (R_SUCCEEDED(rc)) return 0;
        if (R_VALUE(rc) != MAKERESULT(123, 204)) return nplay_tls_failure("handshake", rc);
        av_usleep(10000);
    }
}

static int nplay_tls_transfer(URLContext *h, uint8_t *buf, int size, int writing) {
    TLSContext *ctx = h->priv_data;
    int64_t started = av_gettime_relative();
    if (size < 0 || (!buf && size)) return AVERROR(EINVAL);
    if (!size) return 0;
    for (;;) {
        u32 transferred = 0;
        Result rc;
        int err = nplay_tls_check(h, started);
        if (err) return err;
        rc = writing ? sslConnectionWrite(&ctx->conn, buf, (u32)size, &transferred)
                     : sslConnectionRead(&ctx->conn, buf, (u32)size, &transferred);
        if (R_SUCCEEDED(rc) && (!writing || transferred)) {
            if (transferred > (u32)size) return AVERROR(EIO);
            return (int)transferred; /* zero read is EOF, not reconnect-at-zero */
        }
        if (R_FAILED(rc) && R_VALUE(rc) != MAKERESULT(123, 204))
            return nplay_tls_failure(writing ? "write" : "read", rc);
        if (h->flags & AVIO_FLAG_NONBLOCK) return AVERROR(EAGAIN);
        av_usleep(10000);
    }
}
#endif
