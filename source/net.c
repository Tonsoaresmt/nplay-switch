// net.c - implementacao da camada de HTTP (libcurl).
#include "net.h"
#include "subtitle_limits.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <curl/curl.h>
#include <SDL.h>
#include "cacert_bin.h"
#ifdef __SWITCH__
#include <switch.h>
#endif

// User-Agent de navegador: o Cloudflare do servidor bloqueia UAs "de bot".
// TODO: trocar por "Meruem-Switch/x" + regra de allowlist no Cloudflare.
#define USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
                   "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"

static SDL_atomic_t g_profile_id;
void net_set_profile_id(int profile_id) { SDL_AtomicSet(&g_profile_id, profile_id > 0 ? profile_id : 0); }
int net_get_profile_id(void) { return SDL_AtomicGet(&g_profile_id); }

static struct curl_slist *append_profile_header(struct curl_slist *headers,
                                                const char *url, const char *bearer) {
    extern const char *BASE;
    int profile_id = net_get_profile_id();
    size_t base_len = strlen(BASE);
    if (profile_id <= 0 || !bearer || !bearer[0] || !url ||
        strncmp(url, BASE, base_len) || strncmp(url + base_len, "/api/", 5)) return headers;
    char header[64];
    snprintf(header, sizeof(header), "X-Profile-Id: %d", profile_id);
    return curl_slist_append(headers, header);
}

void membuf_free(struct membuf *m) {
    if (!m) return;
    free(m->data);
    m->data = NULL;
    m->len = 0;
    m->cap = 0;
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    if (size != 0 && nmemb > SIZE_MAX / size) return 0;
    size_t add = size * nmemb;
    struct membuf *m = (struct membuf *)userdata;
    if (m->limit && (m->len > m->limit || add > m->limit - m->len)) return 0;
    if (m->len == SIZE_MAX || add > SIZE_MAX - m->len - 1) return 0;
    size_t need = m->len + add + 1;
    if (need > m->cap) {
        size_t cap = m->cap ? m->cap : 4096;
        while (cap < need) {
            if (cap > SIZE_MAX / 2) { cap = need; break; }
            cap *= 2;
        }
        if (m->limit && m->limit < SIZE_MAX && cap > m->limit + 1) cap = m->limit + 1;
        char *np = realloc(m->data, cap);
        if (!np) return 0;             // sem memoria -> aborta o download
        m->data = np;
        m->cap = cap;
    }
    memcpy(m->data + m->len, ptr, add);
    m->len += add;
    m->data[m->len] = '\0';
    return add;
}

struct file_download_ctx { FILE *file; net_progress_cb progress; void *userdata; };

static size_t file_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct file_download_ctx *ctx = (struct file_download_ctx *)userdata;
    return fwrite(ptr, size, nmemb, ctx->file);
}

static int file_progress_cb(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                            curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    struct file_download_ctx *ctx = (struct file_download_ctx *)userdata;
    return (ctx && ctx->progress) ? ctx->progress((long long)dlnow, (long long)dltotal, ctx->userdata) : 0;
}

// ---- cache DNS e sessao TLS (CURLSH) ----------------------------------------
// Cada request possui seu handle. Somente DNS e sessoes TLS sao compartilhados
// com locks; pools de conexao concorrentes nao sao suportados pelo libcurl.
static CURLSH *g_share = NULL;
static SDL_mutex *g_share_mtx[CURL_LOCK_DATA_LAST];
#ifdef __SWITCH__
// devkitPro curl 7.69/libnx supports SSL_CTX_FUNCTION, not CAINFO_BLOB.
// Import the embedded Mozilla bundle without depending on a writable/readable SD.
// Buffers are built once before workers start, are read-only afterwards, and are
// freed only after the workers have joined.
//
// Importacao em estagios. O E1 replica EXATAMENTE o que o curl/libnx fazia com
// CAINFO apontando para o arquivo da microSD (que funcionou por semanas): bundle
// completo, tamanho exato do arquivo, SEM byte NUL final. A 0.12.51 passou a
// enviar tamanho+1 (NUL depois do ultimo END CERTIFICATE), que um parser PEM pode
// rejeitar por inteiro. Os estagios seguintes so rodam se o E1 falhar:
//   E2: so as raizes prioritarias (PEM puro, sem comentarios), numa chamada;
//   E3: essas mesmas raizes uma a uma (um certificado ruim nao derruba os demais).
// Se nada importar, falha fechada (77); peer, hostname e data seguem verificados.
static unsigned char *g_ca_pem;                 // copia heap do bundle completo
static unsigned char *g_ca_sub;                 // PEM puro das raizes prioritarias
static unsigned int g_ca_sub_len;
#define CA_SUB_MAX 64                           // capacidade (a lista prioritaria tem ~55)
static struct { unsigned int off, len; } g_ca_blk[CA_SUB_MAX];
static unsigned int g_ca_blk_n;
static _Thread_local unsigned int g_tls_import_error;   // 1o resultado nativo que falhou
static _Thread_local unsigned int g_tls_import_stage;   // ultimo estagio tentado (1..3)

// Nomes exatos como aparecem no bundle (linha acima de "=====" em cada bloco).
// Cobrem os servidores do app (Google Trust Services, Sectigo/USERTrust, Let's
// Encrypt) e as CAs mais usadas por CDNs. Fora da lista: so o E1 as importa.
static const char *const k_ca_priority[] = {
    "GTS Root R1", "GTS Root R3", "GTS Root R4",
    "ISRG Root X1", "ISRG Root X2",
    "USERTrust RSA Certification Authority", "USERTrust ECC Certification Authority",
    "COMODO RSA Certification Authority", "COMODO ECC Certification Authority",
    "Sectigo Public Server Authentication Root E46", "Sectigo Public Server Authentication Root R46",
    "GlobalSign Root CA - R3", "GlobalSign ECC Root CA - R5", "GlobalSign Root CA - R6",
    "GlobalSign Root R46", "GlobalSign Root E46", "GlobalSign ECC Root CA - R4",
    "DigiCert Global Root G2", "DigiCert Global Root G3", "DigiCert Trusted Root G4",
    "DigiCert Assured ID Root G2", "DigiCert Assured ID Root G3",
    "DigiCert TLS ECC P384 Root G5", "DigiCert TLS RSA4096 Root G5",
    "Amazon Root CA 1", "Amazon Root CA 2", "Amazon Root CA 3", "Amazon Root CA 4",
    "Go Daddy Root Certificate Authority - G2", "Starfield Root Certificate Authority - G2",
    "Starfield Services Root Certificate Authority - G2",
    "Microsoft ECC Root Certificate Authority 2017", "Microsoft RSA Root Certificate Authority 2017",
    "IdenTrust Commercial Root CA 1", "IdenTrust Public Sector Root CA 1",
    "SSL.com Root Certification Authority RSA", "SSL.com Root Certification Authority ECC",
    "SSL.com TLS RSA Root CA 2022", "SSL.com TLS ECC Root CA 2022",
    "SSL.com EV Root Certification Authority RSA R2", "SSL.com EV Root Certification Authority ECC",
    "QuoVadis Root CA 1 G3", "QuoVadis Root CA 2 G3", "QuoVadis Root CA 3 G3",
    "Certum Trusted Network CA", "Certum Trusted Network CA 2", "Certum Trusted Root CA", "Certum EC-384 CA",
    "Actalis Authentication Root CA", "Buypass Class 2 Root CA", "Buypass Class 3 Root CA",
    "HARICA TLS RSA Root CA 2021", "HARICA TLS ECC Root CA 2021",
    "Certainly Root R1", "Certainly Root E1",
};

static int ca_name_is_priority(const unsigned char *name, size_t len) {
    for (size_t i = 0; i < sizeof(k_ca_priority) / sizeof(k_ca_priority[0]); i++)
        if (strlen(k_ca_priority[i]) == len && !memcmp(k_ca_priority[i], name, len)) return 1;
    return 0;
}

static const unsigned char *ca_find(const unsigned char *p, const unsigned char *limit, const char *needle) {
    size_t n = strlen(needle);
    while (p + n <= limit) {
        p = memchr(p, needle[0], (size_t)(limit - p) - n + 1);
        if (!p) return NULL;
        if (!memcmp(p, needle, n)) return p;
        p++;
    }
    return NULL;
}

// Inicio da linha cujo terminador '\n' esta em nl.
static const unsigned char *ca_line_start(const unsigned char *base, const unsigned char *nl) {
    const unsigned char *p = nl;
    while (p > base && p[-1] != '\n') p--;
    return p;
}

static int ca_is_rule_line(const unsigned char *s, const unsigned char *e) {
    while (e > s && e[-1] == '\r') e--;
    if (e - s < 3) return 0;
    for (; s < e; s++) if (*s != '=') return 0;
    return 1;
}

// Percorre os blocos PEM do bundle (formato curl.se: "Nome\n=====\n-----BEGIN...").
// Com out != NULL copia os blocos prioritarios e registra g_ca_blk. Devolve o total
// de bytes e, em *count, quantos blocos foram selecionados.
static unsigned int ca_scan_priority(unsigned char *out, unsigned int *count) {
    static const char begin_marker[] = "-----BEGIN CERTIFICATE-----";
    static const char end_marker[] = "-----END CERTIFICATE-----";
    const unsigned char *base = cacert_bin, *limit = cacert_bin + cacert_bin_size;
    const unsigned char *p = base, *b;
    unsigned int total = 0, n = 0;
    while ((b = ca_find(p, limit, begin_marker)) != NULL) {
        const unsigned char *e = ca_find(b, limit, end_marker);
        if (!e) break;
        e += sizeof(end_marker) - 1;
        if (e < limit && *e == '\r') e++;
        if (e < limit && *e == '\n') e++;
        int wanted = 0;
        if (b > base && b[-1] == '\n') {
            const unsigned char *rule_end = b - 1;
            const unsigned char *rule_start = ca_line_start(base, rule_end);
            if (rule_start > base && ca_is_rule_line(rule_start, rule_end)) {
                const unsigned char *name_end = rule_start - 1;
                const unsigned char *name_start = ca_line_start(base, name_end);
                while (name_end > name_start && name_end[-1] == '\r') name_end--;
                wanted = ca_name_is_priority(name_start, (size_t)(name_end - name_start));
            }
        }
        if (wanted) {
            unsigned int len = (unsigned int)(e - b);
            if (out && n < CA_SUB_MAX) {
                memcpy(out + total, b, len);
                g_ca_blk[n].off = total;
                g_ca_blk[n].len = len;
            }
            total += len;
            n++;
        }
        p = e;
    }
    if (count) *count = n;
    return total;
}

static int prepare_ca_memory(void) {
    if (g_ca_pem) return 0;
    if (!cacert_bin_size || cacert_bin_size >= UINT32_MAX) return -1;
    g_ca_pem = malloc((size_t)cacert_bin_size + 1);
    if (!g_ca_pem) return -1;
    memcpy(g_ca_pem, cacert_bin, cacert_bin_size);
    g_ca_pem[cacert_bin_size] = 0;   // guarda de leitura; o NUL NAO e enviado ao servico SSL
    // Subconjunto de fallback: opcional, falhar aqui nao impede o E1.
    unsigned int n = 0, len = ca_scan_priority(NULL, &n);
    if (len && n && n <= CA_SUB_MAX && (g_ca_sub = malloc((size_t)len + 1)) != NULL) {
        ca_scan_priority(g_ca_sub, NULL);
        g_ca_sub[len] = 0;
        g_ca_sub_len = len;
        g_ca_blk_n = n;
    }
    return 0;
}

static CURLcode switch_ca_context(CURL *curl, void *ssl_ctx, void *userdata) {
    (void)curl; (void)userdata;
    g_tls_import_error = 0;
    g_tls_import_stage = 0;
    const curl_version_info_data *version = curl_version_info(CURLVERSION_NOW);
    if (!version || !version->ssl_version || strcmp(version->ssl_version, "libnx"))
        return CURLE_SSL_CACERT_BADFILE; // never cast another backend's context
    if (!g_ca_pem) return CURLE_OUT_OF_MEMORY;
    if (!ssl_ctx) return CURLE_SSL_CACERT_BADFILE;
    SslContext *ctx = (SslContext *)ssl_ctx;

    // E1: igual ao curl/libnx com CAINFO (tamanho exato do arquivo, sem NUL).
    g_tls_import_stage = 1;
    Result rc = sslContextImportServerPki(ctx, g_ca_pem, (u32)cacert_bin_size,
                                          SslCertificateFormat_Pem, NULL);
    if (!R_FAILED(rc)) return CURLE_OK;
    g_tls_import_error = rc;   // causa raiz = primeira falha; os estagios abaixo nao a sobrescrevem

    if (g_ca_sub && g_ca_sub_len) {
        g_tls_import_stage = 2;
        if (!R_FAILED(sslContextImportServerPki(ctx, g_ca_sub, g_ca_sub_len,
                                                SslCertificateFormat_Pem, NULL))) {
            g_tls_import_error = 0;   // recuperado pelo fallback
            return CURLE_OK;
        }
        g_tls_import_stage = 3;
        unsigned int imported = 0;
        for (unsigned int i = 0; i < g_ca_blk_n; i++) {
            if (!R_FAILED(sslContextImportServerPki(ctx, g_ca_sub + g_ca_blk[i].off, g_ca_blk[i].len,
                                                    SslCertificateFormat_Pem, NULL))) imported++;
        }
        if (imported) { g_tls_import_error = 0; return CURLE_OK; }
    }
    return CURLE_SSL_CACERT_BADFILE;
}
#else
static int g_ca_ready = 0;
static const char *g_ca_path = "sdmc:/switch/.nplay-ca.pem";

static int ca_file_matches(void) {
    unsigned char buffer[8192];
    size_t offset = 0, count;
    FILE *file = fopen(g_ca_path, "rb");
    if (!file) return 0;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (offset + count > cacert_bin_size ||
            memcmp(buffer, cacert_bin + offset, count) != 0) {
            fclose(file);
            return 0;
        }
        offset += count;
    }
    int matches = !ferror(file) && offset == cacert_bin_size;
    fclose(file);
    return matches;
}

static int provision_ca_bundle(void) {
    static const char *tmp_path = "sdmc:/switch/.nplay-ca.pem.new";
    if (ca_file_matches()) return 0;
    FILE *file = fopen(tmp_path, "wb");
    if (!file) return -1;
    size_t written = fwrite(cacert_bin, 1, cacert_bin_size, file);
    int failed = written != cacert_bin_size || fflush(file) != 0 || ferror(file);
    if (fclose(file) != 0) failed = 1;
    if (failed) { remove(tmp_path); return -1; }
    remove(g_ca_path);
    if (rename(tmp_path, g_ca_path) != 0) { remove(tmp_path); return -1; }
    return ca_file_matches() ? 0 : -1;
}
#endif

static const char *net_transport_error(CURLcode code) {
#ifdef __SWITCH__
    static _Thread_local char message[128];
    if (code == CURLE_SSL_CACERT_BADFILE && g_tls_import_error) {
        // Curta de proposito: a tela de login corta ~26 caracteres e escondia o codigo.
        // "TLS 77/<resultado nativo> E<estagio>"; so numeros, nunca URL, token ou caminho.
        snprintf(message, sizeof(message), "TLS 77/%08X E%u", g_tls_import_error, g_tls_import_stage);
        return message;
    }
#endif
    if (code == CURLE_SSL_CACERT_BADFILE)
        return "Certificados HTTPS indisponiveis (77). Reinstale o Nplay atualizado.";
    if (code == CURLE_PEER_FAILED_VERIFICATION)
        return "Certificado HTTPS nao validado (60). Confira data e hora do console.";
    return curl_easy_strerror(code);
}

static void share_lock(CURL *h, curl_lock_data data, curl_lock_access acc, void *u) {
    (void)h; (void)acc; (void)u;
    if ((int)data >= 0 && data < CURL_LOCK_DATA_LAST && g_share_mtx[data]) SDL_LockMutex(g_share_mtx[data]);
}
static void share_unlock(CURL *h, curl_lock_data data, void *u) {
    (void)h; (void)u;
    if ((int)data >= 0 && data < CURL_LOCK_DATA_LAST && g_share_mtx[data]) SDL_UnlockMutex(g_share_mtx[data]);
}

int net_init(void) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) return -1;
#ifdef __SWITCH__
    prepare_ca_memory(); // callback fails closed with OUT_OF_MEMORY on failure
#else
    g_ca_ready = provision_ca_bundle() == 0;
#endif
    int locks_ready = 1;
    for (int i = 0; i < CURL_LOCK_DATA_LAST; i++) {
        g_share_mtx[i] = SDL_CreateMutex();
        if (!g_share_mtx[i]) locks_ready = 0;
    }
    g_share = locks_ready ? curl_share_init() : NULL;
    if (g_share) {
        curl_share_setopt(g_share, CURLSHOPT_LOCKFUNC, share_lock);
        curl_share_setopt(g_share, CURLSHOPT_UNLOCKFUNC, share_unlock);
        // libcurl explicitly forbids sharing connection pools between concurrent
        // threads, even with lock callbacks. DNS/TLS session sharing is supported.
        curl_share_setopt(g_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(g_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    }
    return 0;   // segue mesmo sem share (so perde o reuso de conexao)
}

void net_exit(void) {
    if (g_share) { curl_share_cleanup(g_share); g_share = NULL; }
    for (int i = 0; i < CURL_LOCK_DATA_LAST; i++) {
        if (g_share_mtx[i]) { SDL_DestroyMutex(g_share_mtx[i]); g_share_mtx[i] = NULL; }
    }
    curl_global_cleanup();
#ifdef __SWITCH__
    free(g_ca_pem);
    g_ca_pem = NULL;
    free(g_ca_sub);
    g_ca_sub = NULL;
    g_ca_sub_len = 0;
    g_ca_blk_n = 0;
#else
    g_ca_ready = 0;
#endif
}

void net_configure_curl_isolated(CURL *curl) {
    if (!curl) return;
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);   // mantem a conexao viva
#ifdef __SWITCH__
    g_tls_import_error = 0;
    g_tls_import_stage = 0;
    // Fail closed even if the linked backend unexpectedly lacks the callback.
    curl_easy_setopt(curl, CURLOPT_CAINFO, "nplay-embedded-ca-only");
    curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);
    curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, switch_ca_context);
#else
    if (g_ca_ready) curl_easy_setopt(curl, CURLOPT_CAINFO, g_ca_path);
#endif
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
}

void net_configure_curl(CURL *curl) {
    if (!curl) return;
    if (g_share) curl_easy_setopt(curl, CURLOPT_SHARE, g_share);
    net_configure_curl_isolated(curl);
}

static int request_cancel_cb(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    return SDL_AtomicGet((SDL_atomic_t *)userdata) ? 1 : 0;
}

long net_request_timeout_cancel(const char *url, const char *method,
                                const char *body, const char *bearer,
                                struct membuf *out, const char **err,
                                long connect_timeout, long total_timeout,
                                SDL_atomic_t *cancel) {
    if (err) *err = NULL;
    if (cancel && SDL_AtomicGet(cancel)) return -(long)CURLE_ABORTED_BY_CALLBACK;

    CURL *curl = curl_easy_init();
    if (!curl) { if (err) *err = "curl_easy_init falhou"; return -1; }

    // Sem "Accept" fixo: assim o mesmo helper serve para JSON e para imagens.
    struct curl_slist *headers = NULL;
    if (body) headers = curl_slist_append(headers, "Content-Type: application/json");

    char authbuf[1024];
    if (bearer && bearer[0]) {
        snprintf(authbuf, sizeof(authbuf), "Authorization: Bearer %s", bearer);
        headers = curl_slist_append(headers, authbuf);
    }
    headers = append_profile_header(headers, url, bearer);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, total_timeout);
    // Validação TLS reativada conforme política de segurança.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    if (cancel) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, request_cancel_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancel);
    }
    net_configure_curl(curl);

    if (method && strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
    } else if (method && strcmp(method, "GET") != 0) {
        // DELETE/PUT/etc: metodo custom. Se veio corpo, manda como campos.
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
        if (body) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }

    CURLcode res = curl_easy_perform(curl);
    long code;
    if (res != CURLE_OK) {
        if (err) *err = net_transport_error(res);
        code = -(long)res;
    } else {
        code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);   // conexao volta pro cache compartilhado (nao fecha)
    return code;
}

long net_request_timeout(const char *url, const char *method,
                         const char *body, const char *bearer,
                         struct membuf *out, const char **err,
                         long connect_timeout, long total_timeout) {
    return net_request_timeout_cancel(url, method, body, bearer, out, err,
                                      connect_timeout, total_timeout, NULL);
}

typedef struct { struct membuf *out; size_t limit; SDL_atomic_t *cancel; } LimitedText;
static size_t limited_text_write(char *ptr, size_t size, size_t count, void *opaque) {
    LimitedText *ctx = opaque;
    if ((ctx->cancel && SDL_AtomicGet(ctx->cancel)) ||
        (size && count > SIZE_MAX / size)) return 0;
    size_t bytes = size * count;
    if (ctx->out->len > ctx->limit || bytes > ctx->limit - ctx->out->len) return 0;
    size_t need = ctx->out->len + bytes + 1;
    if (need > ctx->out->cap) {
        size_t cap = ctx->out->cap ? ctx->out->cap : 4096;
        while (cap < need) cap *= 2; // Bounded by SUBTITLE_DOWNLOAD_MAX.
        if (cap > ctx->limit + 1) cap = ctx->limit + 1;
        char *grown = realloc(ctx->out->data, cap);
        if (!grown) return 0;
        ctx->out->data = grown; ctx->out->cap = cap;
    }
    memcpy(ctx->out->data + ctx->out->len, ptr, bytes);
    ctx->out->len += bytes; ctx->out->data[ctx->out->len] = 0;
    return bytes;
}

long net_get_text_limited(const char *url, size_t limit, long timeout_ms,
                           SDL_atomic_t *cancel, struct membuf *out) {
    if (!out || out->data || !url || strncmp(url, "https://", 8) ||
        !limit || limit > SUBTITLE_DOWNLOAD_MAX || timeout_ms <= 0 ||
        (cancel && SDL_AtomicGet(cancel))) return -1;
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    LimitedText ctx = {out, limit, cancel};
    net_configure_curl_isolated(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)limit);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, limited_text_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    if (cancel) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, request_cancel_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancel);
    }
    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    if (rc == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    else code = -(long)rc;
    curl_easy_cleanup(curl);
    if (code != 200 || (cancel && SDL_AtomicGet(cancel))) {
        membuf_free(out);
        if (code == 200) code = -1;
    }
    return code;
}

long net_request(const char *url, const char *method,
                 const char *body, const char *bearer,
                 struct membuf *out, const char **err) {
    return net_request_timeout(url, method, body, bearer, out, err, 15L, 45L);
}

typedef struct {
    CURL *curl; size_t received, limit; Uint32 last_byte;
    SDL_atomic_t *cancel; net_text_chunk_cb callback; void *userdata;
} TextStream;
static size_t text_stream_write(char *data,size_t size,size_t count,void *opaque) {
    TextStream *s=opaque;
    if((size&&count>SIZE_MAX/size)||SDL_AtomicGet(s->cancel))return 0;
    size_t bytes=size*count;
    if(s->received>s->limit||bytes>s->limit-s->received)return 0;
    s->last_byte=SDL_GetTicks();s->received+=bytes;
    long status=0;curl_easy_getinfo(s->curl,CURLINFO_RESPONSE_CODE,&status);
    // Preserve the HTTP status for the caller.  Aborting the body on a 404/503
    // turns it into CURLE_WRITE_ERROR and hides the actionable server result.
    if(status&&status!=200)return bytes;
    if(status!=200)return 0;
    return s->callback(data,bytes,s->userdata)?bytes:0;
}
static int text_stream_progress(void *opaque,curl_off_t a,curl_off_t b,curl_off_t c,curl_off_t d) {
    (void)a;(void)b;(void)c;(void)d;TextStream *s=opaque;
    return SDL_AtomicGet(s->cancel)||SDL_GetTicks()-s->last_byte>90000u;
}
long net_stream_text(const char *url,size_t limit,SDL_atomic_t *cancel,
                      net_text_chunk_cb callback,void *userdata) {
    if(!url||strncmp(url,"https://",8)||!cancel||!callback||!limit||limit>SUBTITLE_DOWNLOAD_MAX||SDL_AtomicGet(cancel))return -1;
    CURL *curl=curl_easy_init();if(!curl)return -1;
    TextStream stream={.curl=curl,.limit=limit,.last_byte=SDL_GetTicks(),.cancel=cancel,.callback=callback,.userdata=userdata};
    net_configure_curl_isolated(curl);
    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,USER_AGENT);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,2500L);
    curl_easy_setopt(curl,CURLOPT_MAXFILESIZE_LARGE,(curl_off_t)limit);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,text_stream_write);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&stream);
    curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
    curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,text_stream_progress);
    curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&stream);
    CURLcode rc=curl_easy_perform(curl);long code=0;
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&code);
    curl_easy_cleanup(curl);
    if(rc!=CURLE_OK)return -(long)rc;
    return code;
}

long net_download_file_timeout(const char *url, const char *bearer,
                               const char *path, const char **err,
                               long connect_timeout, long total_timeout) {
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    char authbuf[1024];
    FILE *f;
    struct file_download_ctx dlctx = {0};
    long code = -1;

    if (err) *err = NULL;
    if (!path) {
        if (err) *err = "path invalido";
        return -1;
    }

    f = fopen(path, "wb");
    if (!f) {
        if (err) *err = "fopen falhou";
        return -1;
    }
    dlctx.file = f;

    curl = curl_easy_init();
    if (!curl) {
        fclose(f);
        if (err) *err = "curl_easy_init falhou";
        return -1;
    }

    if (bearer && bearer[0]) {
        snprintf(authbuf, sizeof(authbuf), "Authorization: Bearer %s", bearer);
        headers = curl_slist_append(headers, authbuf);
    }
    headers = append_profile_header(headers, url, bearer);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, total_timeout);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 256L * 1024L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dlctx);
    net_configure_curl(curl);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        if (err) *err = net_transport_error(res);
        code = -(long)res;
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    fclose(f);

    if (code != 200) remove(path);
    return code;
}

long net_download_file_progress(const char *url, const char *bearer,
                                const char *path, const char **err,
                                net_progress_cb progress, void *userdata) {
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    char authbuf[1024];
    long code = -1;
    FILE *f = NULL;
    struct file_download_ctx dlctx = {0};
    if (err) *err = NULL;
    if (!url || !path) { if (err) *err = "parametro invalido"; return -1; }
    f = fopen(path, "wb");
    if (!f) { if (err) *err = "nao foi possivel criar arquivo na microSD"; return -1; }
    dlctx.file = f; dlctx.progress = progress; dlctx.userdata = userdata;
    curl = curl_easy_init();
    if (!curl) { fclose(f); if (err) *err = "curl_easy_init falhou"; return -1; }
    if (bearer && bearer[0]) {
        snprintf(authbuf, sizeof(authbuf), "Authorization: Bearer %s", bearer);
        headers = curl_slist_append(headers, authbuf);
    }
    headers = append_profile_header(headers, url, bearer);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 45L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 256L * 1024L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dlctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, file_progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &dlctx);
    net_configure_curl(curl);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) { if (err) *err = net_transport_error(res); code = -(long)res; }
    else curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    fclose(f);
    if (code != 200) remove(path);
    return code;
}

long net_download_file(const char *url, const char *bearer,
                       const char *path, const char **err) {
    return net_download_file_timeout(url, bearer, path, err, 15L, 180L);
}

void net_urlencode(const char *in, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!in || !in[0]) return;
    char *enc = curl_easy_escape(NULL, in, 0);
    if (enc) {
        snprintf(out, cap, "%s", enc);
        curl_free(enc);
    }
}
