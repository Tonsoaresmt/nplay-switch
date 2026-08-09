// store.c - token + historico de leitura no SD (sdmc:/switch/Meruem).
// progress.json: { "<bookId>": { p, sid, st, cl, pb, cv, pg, ts }, ... }
#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "cJSON.h"

#define DIR_BASE "sdmc:/switch"
#define DIR_APP  "sdmc:/switch/Meruem"
#define TOKEN_F  DIR_APP "/token.txt"
#define SERVER_F DIR_APP "/server.txt"
#define USER_F   DIR_APP "/user.txt"
#define SEEN_F   DIR_APP "/update_seen.txt"
#define LOCAL_F  DIR_APP "/local_root.txt"
#define AREA_F   DIR_APP "/last_area.txt"
#define AREAS_F  DIR_APP "/hidden_areas.txt"
#define ORIENT_F DIR_APP "/orientation.txt"
#define DOCNIGHT_F DIR_APP "/doc_night.txt"
#define PROG_F   DIR_APP "/progress.json"
#define OFFSER_F DIR_APP "/offline_series.json"
#define FITM_F   DIR_APP "/fit_modes.json"
#define PREFA_F  DIR_APP "/pref_audio.txt"
#define PREFS_F  DIR_APP "/pref_sub.txt"

static cJSON *g_prog = NULL;
static cJSON *g_offser = NULL;   // { seriesId: estado offline 0/1/2 }
static cJSON *g_fitm = NULL;     // { seriesId: modo de ajuste 0=Auto 1=Conter 2=Largura }

void store_init(void) {
    mkdir(DIR_BASE, 0777);
    mkdir(DIR_APP, 0777);
    FILE *f = fopen(PROG_F, "rb");
    if (f) {
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        if (n > 0 && n < 8 * 1024 * 1024) {
            char *buf = (char *)malloc((size_t)n + 1);
            if (buf) { size_t rd = fread(buf, 1, (size_t)n, f); buf[rd] = '\0'; g_prog = cJSON_Parse(buf); free(buf); }
        }
        fclose(f);
    }
    if (!g_prog) g_prog = cJSON_CreateObject();

    FILE *fo = fopen(OFFSER_F, "rb");
    if (fo) {
        fseek(fo, 0, SEEK_END); long n2 = ftell(fo); fseek(fo, 0, SEEK_SET);
        if (n2 > 0 && n2 < 4 * 1024 * 1024) {
            char *b2 = (char *)malloc((size_t)n2 + 1);
            if (b2) { size_t rd = fread(b2, 1, (size_t)n2, fo); b2[rd] = '\0'; g_offser = cJSON_Parse(b2); free(b2); }
        }
        fclose(fo);
    }
    if (!g_offser) g_offser = cJSON_CreateObject();

    FILE *ff = fopen(FITM_F, "rb");
    if (ff) {
        fseek(ff, 0, SEEK_END); long n3 = ftell(ff); fseek(ff, 0, SEEK_SET);
        if (n3 > 0 && n3 < 2 * 1024 * 1024) {
            char *b3 = (char *)malloc((size_t)n3 + 1);
            if (b3) { size_t rd = fread(b3, 1, (size_t)n3, ff); b3[rd] = '\0'; g_fitm = cJSON_Parse(b3); free(b3); }
        }
        fclose(ff);
    }
    if (!g_fitm) g_fitm = cJSON_CreateObject();
}

int store_get_fit_mode(const char *seriesId, int fallback) {
    if (!g_fitm || !seriesId || !seriesId[0]) return fallback;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(g_fitm, seriesId);
    return cJSON_IsNumber(it) ? it->valueint : fallback;
}

void store_set_fit_mode(const char *seriesId, int mode) {
    if (!g_fitm || !seriesId || !seriesId[0]) return;
    cJSON_DeleteItemFromObjectCaseSensitive(g_fitm, seriesId);
    if (mode > 0) cJSON_AddNumberToObject(g_fitm, seriesId, mode);  // 0=Auto = default, nao guarda
    char *s = cJSON_PrintUnformatted(g_fitm);
    if (s) {
        FILE *f = fopen(FITM_F, "wb");
        if (f) { fwrite(s, 1, strlen(s), f); fclose(f); }
        free(s);
    }
}

int store_get_series_offline(const char *seriesId) {
    if (!g_offser || !seriesId || !seriesId[0]) return 0;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(g_offser, seriesId);
    return cJSON_IsNumber(it) ? it->valueint : 0;
}

void store_set_series_offline(const char *seriesId, int state) {
    if (!g_offser || !seriesId || !seriesId[0]) return;
    cJSON_DeleteItemFromObjectCaseSensitive(g_offser, seriesId);
    if (state > 0) cJSON_AddNumberToObject(g_offser, seriesId, state);
    char *s = cJSON_PrintUnformatted(g_offser);
    if (s) {
        FILE *f = fopen(OFFSER_F, "wb");
        if (f) { fwrite(s, 1, strlen(s), f); fclose(f); }
        free(s);
    }
}

void store_clear_series_offline_all(void) {
    if (g_offser) cJSON_Delete(g_offser);
    g_offser = cJSON_CreateObject();
    remove(OFFSER_F);
}

int store_load_token(char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    FILE *f = fopen(TOKEN_F, "rb");
    if (!f) return 0;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = '\0';
    size_t len = strlen(out);
    while (len > 0 && (out[len-1]=='\n'||out[len-1]=='\r'||out[len-1]==' '||out[len-1]=='\t')) out[--len] = '\0';
    return len > 0 ? 1 : 0;
}
void store_save_token(const char *token) {
    if (!token) return;
    FILE *f = fopen(TOKEN_F, "wb");
    if (!f) return;
    fwrite(token, 1, strlen(token), f);
    fclose(f);
}
void store_clear_token(void) { remove(TOKEN_F); }

static void trim_line(char *s) {
    size_t len;
    if (!s) return;
    len = strlen(s);
    while (len > 0 && (s[len-1]=='\n'||s[len-1]=='\r'||s[len-1]==' '||s[len-1]=='\t'||s[len-1]=='/')) {
        s[--len] = '\0';
    }
}

int store_load_pref_audio(char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    FILE *f = fopen(PREFA_F, "rb");
    if (!f) return 0;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = '\0';
    trim_line(out);
    return strlen(out) > 0 ? 1 : 0;
}
void store_save_pref_audio(const char *lang) {
    if (!lang) return;
    FILE *f = fopen(PREFA_F, "wb");
    if (!f) return;
    fwrite(lang, 1, strlen(lang), f);
    fclose(f);
}

int store_load_pref_sub(char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    FILE *f = fopen(PREFS_F, "rb");
    if (!f) return 0;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = '\0';
    trim_line(out);
    return strlen(out) > 0 ? 1 : 0;
}
void store_save_pref_sub(const char *lang) {
    if (!lang) return;
    FILE *f = fopen(PREFS_F, "wb");
    if (!f) return;
    fwrite(lang, 1, strlen(lang), f);
    fclose(f);
}



int store_load_server(char *out, size_t cap) {
    FILE *f;
    size_t n;
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    f = fopen(SERVER_F, "rb");
    if (!f) return 0;
    n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = '\0';
    trim_line(out);
    return out[0] ? 1 : 0;
}

void store_save_server(const char *url) {
    FILE *f;
    char clean[256];
    if (!url || !url[0]) return;
    snprintf(clean, sizeof(clean), "%s", url);
    trim_line(clean);
    f = fopen(SERVER_F, "wb");
    if (!f) return;
    fwrite(clean, 1, strlen(clean), f);
    fclose(f);
}

int store_load_user(char *out, size_t cap) {
    FILE *f;
    size_t n;
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    f = fopen(USER_F, "rb");
    if (!f) return 0;
    n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = '\0';
    trim_line(out);
    return out[0] ? 1 : 0;
}

void store_save_user(const char *user) {
    FILE *f;
    char clean[128];
    if (!user || !user[0]) return;
    snprintf(clean, sizeof(clean), "%s", user);
    trim_line(clean);
    f = fopen(USER_F, "wb");
    if (!f) return;
    fwrite(clean, 1, strlen(clean), f);
    fclose(f);
}

void store_clear_user(void) { remove(USER_F); }

int store_load_update_seen(char *out, size_t cap) {
    FILE *f;
    size_t n;
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    f = fopen(SEEN_F, "rb");
    if (!f) return 0;
    n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = '\0';
    trim_line(out);
    return out[0] ? 1 : 0;
}

void store_save_update_seen(const char *tag) {
    FILE *f;
    char clean[64];
    if (!tag || !tag[0]) return;
    snprintf(clean, sizeof(clean), "%s", tag);
    trim_line(clean);
    f = fopen(SEEN_F, "wb");
    if (!f) return;
    fwrite(clean, 1, strlen(clean), f);
    fclose(f);
}

int store_load_local_root(char *out, size_t cap) {
    FILE *f;
    size_t n;
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    f = fopen(LOCAL_F, "rb");
    if (!f) return 0;
    n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = '\0';
    trim_line(out);
    if (strcmp(out, "sdmc:") == 0 && cap > 6) snprintf(out, cap, "sdmc:/");
    return out[0] ? 1 : 0;
}

void store_save_local_root(const char *path) {
    FILE *f;
    char clean[512];
    if (!path || !path[0]) return;
    snprintf(clean, sizeof(clean), "%s", path);
    trim_line(clean);
    if (strcmp(clean, "sdmc:") == 0) snprintf(clean, sizeof(clean), "sdmc:/");
    f = fopen(LOCAL_F, "wb");
    if (!f) return;
    fwrite(clean, 1, strlen(clean), f);
    fclose(f);
}

int store_load_last_area(char *out, size_t cap) {
    FILE *f;
    size_t n;
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    f = fopen(AREA_F, "rb");
    if (!f) return 0;
    n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = '\0';
    trim_line(out);
    return out[0] ? 1 : 0;
}

void store_save_last_area(const char *area) {
    FILE *f;
    char clean[32];
    if (!area || !area[0]) return;
    snprintf(clean, sizeof(clean), "%s", area);
    trim_line(clean);
    f = fopen(AREA_F, "wb");
    if (!f) return;
    fwrite(clean, 1, strlen(clean), f);
    fclose(f);
}

int store_load_area_hidden_mask(unsigned *mask) {
    FILE *f;
    char buf[32];
    size_t n;
    if (!mask) return 0;
    *mask = 0;
    f = fopen(AREAS_F, "rb");
    if (!f) return 0;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    trim_line(buf);
    if (!buf[0]) return 0;
    *mask = (unsigned)strtoul(buf, NULL, 10);
    return 1;
}

void store_save_area_hidden_mask(unsigned mask) {
    FILE *f = fopen(AREAS_F, "wb");
    char buf[32];
    if (!f) return;
    snprintf(buf, sizeof(buf), "%u", mask);
    fwrite(buf, 1, strlen(buf), f);
    fclose(f);
}

int store_load_orientation(int *portrait) {
    FILE *f;
    char buf[16];
    size_t n;
    if (!portrait) return 0;
    f = fopen(ORIENT_F, "rb");
    if (!f) return 0;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    trim_line(buf);
    if (!buf[0]) return 0;
    *portrait = (int)strtol(buf, NULL, 10) ? 1 : 0;
    return 1;
}

void store_save_orientation(int portrait) {
    FILE *f = fopen(ORIENT_F, "wb");
    if (!f) return;
    fwrite(portrait ? "1" : "0", 1, 1, f);
    fclose(f);
}

int store_load_doc_night(int *enabled) {
    FILE *f;
    char buf[16];
    size_t n;
    if (!enabled) return 0;
    f = fopen(DOCNIGHT_F, "rb");
    if (!f) return 0;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    trim_line(buf);
    if (!buf[0]) return 0;
    *enabled = (int)strtol(buf, NULL, 10) ? 1 : 0;
    return 1;
}

void store_save_doc_night(int enabled) {
    FILE *f = fopen(DOCNIGHT_F, "wb");
    if (!f) return;
    fwrite(enabled ? "1" : "0", 1, 1, f);
    fclose(f);
}

void store_flush(void) {
    if (!g_prog) return;
    char *s = cJSON_PrintUnformatted(g_prog);
    if (!s) return;
    FILE *f = fopen(PROG_F, "wb");
    if (f) { fwrite(s, 1, strlen(s), f); fclose(f); }
    free(s);
}

int store_get_progress(const char *bookId) {
    if (!g_prog || !bookId || !bookId[0]) return 1;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(g_prog, bookId);
    if (!it) return 1;
    if (cJSON_IsNumber(it)) return it->valueint >= 1 ? it->valueint : 1;  // formato antigo
    cJSON *p = cJSON_GetObjectItemCaseSensitive(it, "p");
    int v = cJSON_IsNumber(p) ? p->valueint : 1;
    return v >= 1 ? v : 1;
}

int store_get_doc_scale(const char *bookId, int fallback) {
    if (fallback < 0) fallback = 0;
    if (fallback > 3) fallback = 3;
    if (!g_prog || !bookId || !bookId[0]) return fallback;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(g_prog, bookId);
    if (!it || !cJSON_IsObject(it)) return fallback;
    cJSON *x = cJSON_GetObjectItemCaseSensitive(it, "ds");
    if (!cJSON_IsNumber(x)) return fallback;
    int v = x->valueint;
    if (v < 0) v = 0;
    if (v > 3) v = 3;
    return v;
}

static void jset_num(cJSON *o, const char *k, double v) {
    cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    if (it) cJSON_SetNumberValue(it, v);
    else cJSON_AddNumberToObject(o, k, v);
}
static void jset_str(cJSON *o, const char *k, const char *v) {
    cJSON_DeleteItemFromObjectCaseSensitive(o, k);
    cJSON_AddStringToObject(o, k, v ? v : "");
}

void store_record(const char *bookId, int page, const char *seriesId,
                  const char *seriesTitle, const char *chapLabel,
                  const char *pageBase, const char *seriesCover, int pages) {
    if (!g_prog || !bookId || !bookId[0]) return;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(g_prog, bookId);
    if (!it || !cJSON_IsObject(it)) {
        if (it) cJSON_DeleteItemFromObjectCaseSensitive(g_prog, bookId);  // descarta formato antigo
        it = cJSON_CreateObject();
        cJSON_AddItemToObject(g_prog, bookId, it);
    }
    jset_num(it, "p", page);
    jset_str(it, "sid", seriesId);
    jset_str(it, "st", seriesTitle);
    jset_str(it, "cl", chapLabel);
    jset_str(it, "pb", pageBase);
    jset_str(it, "cv", seriesCover);
    jset_num(it, "pg", pages);
    jset_num(it, "ts", (double)time(NULL));
    store_flush();
}

void store_set_doc_scale(const char *bookId, int scale) {
    if (!g_prog || !bookId || !bookId[0]) return;
    if (scale < 0) scale = 0;
    if (scale > 3) scale = 3;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(g_prog, bookId);
    if (!it || !cJSON_IsObject(it)) {
        if (it) cJSON_DeleteItemFromObjectCaseSensitive(g_prog, bookId);
        it = cJSON_CreateObject();
        cJSON_AddItemToObject(g_prog, bookId, it);
    }
    jset_num(it, "ds", scale);
    jset_num(it, "ts", (double)time(NULL));
    store_flush();
}

typedef struct { char id[96]; double ts; } RecentEnt;
static int recent_cmp(const void *a, const void *b) {
    double ta = ((const RecentEnt *)a)->ts, tb = ((const RecentEnt *)b)->ts;
    return (tb > ta) - (tb < ta);  // decrescente
}

int store_recent(char ids[][96], int max) {
    if (!g_prog || max <= 0) return 0;
    int cnt = cJSON_GetArraySize(g_prog);
    if (cnt <= 0) return 0;
    RecentEnt *arr = (RecentEnt *)malloc(sizeof(RecentEnt) * cnt);
    if (!arr) return 0;
    int k = 0;
    for (cJSON *ch = g_prog->child; ch; ch = ch->next) {
        if (!ch->string) continue;
        double ts = 0;
        if (cJSON_IsObject(ch)) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(ch, "ts");
            if (cJSON_IsNumber(t)) ts = t->valuedouble;
        }
        snprintf(arr[k].id, sizeof(arr[k].id), "%s", ch->string);
        arr[k].ts = ts;
        k++;
    }
    qsort(arr, k, sizeof(RecentEnt), recent_cmp);
    int out = 0;
    for (int i = 0; i < k && out < max; i++) { snprintf(ids[out], 96, "%s", arr[i].id); out++; }
    free(arr);
    return out;
}

int store_entry(const char *bookId,
                char *seriesId, size_t sidCap,
                char *seriesTitle, size_t stCap,
                char *chapLabel, size_t clCap,
                char *pageBase, size_t pbCap,
                char *seriesCover, size_t cvCap,
                int *page, int *pages) {
    if (seriesId && sidCap) seriesId[0] = '\0';
    if (seriesTitle && stCap) seriesTitle[0] = '\0';
    if (chapLabel && clCap) chapLabel[0] = '\0';
    if (pageBase && pbCap) pageBase[0] = '\0';
    if (seriesCover && cvCap) seriesCover[0] = '\0';
    if (page) *page = 1;
    if (pages) *pages = 1;
    if (!g_prog || !bookId) return 0;
    cJSON *it = cJSON_GetObjectItemCaseSensitive(g_prog, bookId);
    if (!it || !cJSON_IsObject(it)) return 0;
    cJSON *x;
    x = cJSON_GetObjectItemCaseSensitive(it, "sid"); if (seriesId && cJSON_IsString(x))    snprintf(seriesId, sidCap, "%s", x->valuestring);
    x = cJSON_GetObjectItemCaseSensitive(it, "st");  if (seriesTitle && cJSON_IsString(x)) snprintf(seriesTitle, stCap, "%s", x->valuestring);
    x = cJSON_GetObjectItemCaseSensitive(it, "cl");  if (chapLabel && cJSON_IsString(x))   snprintf(chapLabel, clCap, "%s", x->valuestring);
    x = cJSON_GetObjectItemCaseSensitive(it, "pb");  if (pageBase && cJSON_IsString(x))    snprintf(pageBase, pbCap, "%s", x->valuestring);
    x = cJSON_GetObjectItemCaseSensitive(it, "cv");  if (seriesCover && cJSON_IsString(x)) snprintf(seriesCover, cvCap, "%s", x->valuestring);
    x = cJSON_GetObjectItemCaseSensitive(it, "p");   if (page && cJSON_IsNumber(x))        *page = x->valueint;
    x = cJSON_GetObjectItemCaseSensitive(it, "pg");  if (pages && cJSON_IsNumber(x))       *pages = x->valueint;
    return 1;
}
