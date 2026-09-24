// store.c - token + historico de leitura no SD (sdmc:/switch/Meruem).
// progress.json: { "<bookId>": { p, sid, st, cl, pb, cv, pg, ts }, ... }
#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>
#include "cJSON.h"

#define DIR_BASE "sdmc:/switch"
#define DIR_APP  "sdmc:/switch/Meruem"
#define TOKEN_F  DIR_APP "/token.txt"
#define PROFILE_F DIR_APP "/profile_id.txt"
#define MEDIA_LISTS_OWNER_F DIR_APP "/media_lists_owner.txt"
#define MEDIA_LISTS_USER_F DIR_APP "/media_lists_user.txt"
#define SERVER_F DIR_APP "/server.txt"
#define USER_F   DIR_APP "/user.txt"
#define DEVICE_F DIR_APP "/device_id.txt"
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
#define PREFV_F  DIR_APP "/player_volume.txt"
#define PLAYER_STATS_F DIR_APP "/player_stats.txt"
#define MEDIA_LISTS_F DIR_APP "/media_lists.json"
#define MEDIA_LISTS_TMP DIR_APP "/media_lists.json.tmp"
#define MEDIA_LISTS_BAK DIR_APP "/media_lists.json.bak"

static cJSON *g_prog = NULL;
static cJSON *g_offser = NULL;   // { seriesId: estado offline 0/1/2 }
static cJSON *g_fitm = NULL;     // { seriesId: modo de ajuste 0=Auto 1=Conter 2=Largura }
static cJSON *g_media_lists = NULL; // { lists: [{ name, items: [{id,series,title,logo}] }] }
static int g_media_profile_id = 0;
static int g_media_legacy_owner = 0;
static char g_media_legacy_user[128] = {0};

static int read_positive_id(const char *path) {
    char buf[32] = {0};
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    char *end = NULL;
    long id = strtol(buf, &end, 10);
    if (end == buf || id <= 0 || id > 2147483647L) return 0;
    while (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t') end++;
    return *end ? 0 : (int)id;
}

static int write_positive_id(const char *path, int id) {
    if (id <= 0) return 0;
    char tmp[128], bak[128];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp) ||
        snprintf(bak, sizeof(bak), "%s.bak", path) >= (int)sizeof(bak)) return 0;
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    int written = fprintf(f, "%d\n", id) > 0;
    if (fclose(f) != 0) written = 0;
    if (!written) { remove(tmp); return 0; }
    remove(bak);
    int backed_up = rename(path, bak) == 0;
    if (rename(tmp, path) == 0) {
        if (backed_up) remove(bak);
        return 1;
    }
    remove(tmp);
    if (backed_up) rename(bak, path);
    return 0;
}

int store_load_profile_id(int *profile_id) {
    int id = read_positive_id(PROFILE_F);
    if (profile_id) *profile_id = id;
    return id > 0;
}
int store_save_profile_id(int profile_id) { return write_positive_id(PROFILE_F, profile_id); }
void store_clear_profile_id(void) { remove(PROFILE_F); }

static void media_lists_paths(char *path, char *tmp, char *bak, size_t cap) {
    if (g_media_profile_id <= 0 || g_media_profile_id == g_media_legacy_owner) {
        snprintf(path, cap, "%s", MEDIA_LISTS_F);
        snprintf(tmp, cap, "%s", MEDIA_LISTS_TMP);
        snprintf(bak, cap, "%s", MEDIA_LISTS_BAK);
    } else {
        snprintf(path, cap, DIR_APP "/media_lists_%d.json", g_media_profile_id);
        snprintf(tmp, cap, DIR_APP "/media_lists_%d.json.tmp", g_media_profile_id);
        snprintf(bak, cap, DIR_APP "/media_lists_%d.json.bak", g_media_profile_id);
    }
}

static cJSON *load_json_file(const char *path, long max_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    cJSON *json = NULL;
    if (n > 0 && n < max_size) {
        char *buf = (char *)malloc((size_t)n + 1);
        if (buf) { size_t rd = fread(buf, 1, (size_t)n, f); buf[rd] = '\0'; json = cJSON_Parse(buf); free(buf); }
    }
    fclose(f);
    return json;
}

static void save_media_lists(void) {
    if (!g_media_lists) return;
    char *text = cJSON_PrintUnformatted(g_media_lists);
    if (!text) return;
    char path[128], tmp[128], bak[128];
    media_lists_paths(path, tmp, bak, sizeof(path));
    FILE *f = fopen(tmp, "wb");
    size_t len = strlen(text); int written = 0;
    if (f) { written = fwrite(text, 1, len, f) == len; fclose(f); }
    if (written) {
        remove(bak);
        int backed_up = rename(path, bak) == 0;
        if (rename(tmp, path) == 0) {
            if (backed_up) remove(bak);
        } else {
            remove(tmp);
            if (backed_up) rename(bak, path);
        }
    } else remove(tmp);
    free(text);
}

void store_init(void) {
    mkdir(DIR_BASE, 0777);
    mkdir(DIR_APP, 0777);
    g_media_legacy_owner = read_positive_id(MEDIA_LISTS_OWNER_F);
    FILE *owner = fopen(MEDIA_LISTS_USER_F, "rb");
    if (owner) {
        size_t n = fread(g_media_legacy_user, 1, sizeof(g_media_legacy_user) - 1, owner);
        fclose(owner);
        g_media_legacy_user[n] = '\0';
        g_media_legacy_user[strcspn(g_media_legacy_user, "\r\n")] = '\0';
    } else {
        // user.txt da versao anterior identifica quem ja possuia media_lists.json.
        // Registra antes que um novo login possa sobrescrever aquele usuario.
        store_load_user(g_media_legacy_user, sizeof(g_media_legacy_user));
        if (g_media_legacy_user[0]) {
            owner = fopen(MEDIA_LISTS_USER_F, "wb");
            if (owner) { fwrite(g_media_legacy_user, 1, strlen(g_media_legacy_user), owner); fclose(owner); }
        }
    }
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

    char media_path[128], media_tmp[128], media_bak[128];
    media_lists_paths(media_path, media_tmp, media_bak, sizeof(media_path));
    g_media_lists = load_json_file(media_path, 4 * 1024 * 1024);
    if (!g_media_lists || !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(g_media_lists, "lists"))) {
        if (g_media_lists) cJSON_Delete(g_media_lists);
        g_media_lists = cJSON_CreateObject();
        cJSON *lists = cJSON_AddArrayToObject(g_media_lists, "lists");
        cJSON *later = cJSON_CreateObject();
        cJSON_AddStringToObject(later, "name", "Assistir mais tarde");
        cJSON_AddItemToObject(later, "items", cJSON_CreateArray());
        cJSON_AddItemToArray(lists, later);
        save_media_lists();
    }
}

void store_select_profile(int profile_id, const char *username) {
    if (profile_id <= 0 || profile_id == g_media_profile_id) return;
    int may_claim_legacy = !g_media_legacy_user[0] ||
                           (username && !strcmp(g_media_legacy_user, username));
    if (!g_media_legacy_owner && may_claim_legacy) {
        // A lista anterior a perfis pertence ao primeiro perfil escolhido neste
        // usuario. Uma conta diferente nao recebe suas listas privadas.
        if (write_positive_id(MEDIA_LISTS_OWNER_F, profile_id)) {
            g_media_legacy_owner = profile_id;
            if (!g_media_legacy_user[0] && username && username[0]) {
                snprintf(g_media_legacy_user, sizeof(g_media_legacy_user), "%s", username);
                FILE *f = fopen(MEDIA_LISTS_USER_F, "wb");
                if (f) { fwrite(g_media_legacy_user, 1, strlen(g_media_legacy_user), f); fclose(f); }
            }
        }
    }
    g_media_profile_id = profile_id;
    if (g_media_lists) { cJSON_Delete(g_media_lists); g_media_lists = NULL; }
    char path[128], tmp[128], bak[128];
    media_lists_paths(path, tmp, bak, sizeof(path));
    g_media_lists = load_json_file(path, 4 * 1024 * 1024);
    if (!g_media_lists || !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(g_media_lists, "lists"))) {
        if (g_media_lists) cJSON_Delete(g_media_lists);
        g_media_lists = cJSON_CreateObject();
        cJSON *lists = cJSON_AddArrayToObject(g_media_lists, "lists");
        cJSON *later = cJSON_CreateObject();
        cJSON_AddStringToObject(later, "name", "Assistir mais tarde");
        cJSON_AddItemToObject(later, "items", cJSON_CreateArray());
        cJSON_AddItemToArray(lists, later);
        save_media_lists();
    }
}

static cJSON *media_lists_array(void) {
    return g_media_lists ? cJSON_GetObjectItemCaseSensitive(g_media_lists, "lists") : NULL;
}

static cJSON *media_list_at(int index) {
    cJSON *lists = media_lists_array();
    return cJSON_IsArray(lists) ? cJSON_GetArrayItem(lists, index) : NULL;
}

int store_media_list_count(void) { return cJSON_GetArraySize(media_lists_array()); }

const char *store_media_list_name(int list_index) {
    cJSON *name = cJSON_GetObjectItemCaseSensitive(media_list_at(list_index), "name");
    return cJSON_IsString(name) && name->valuestring ? name->valuestring : "Lista";
}

int store_media_list_create(const char *name) {
    cJSON *lists = media_lists_array();
    if (!cJSON_IsArray(lists) || !name || !name[0]) return -1;
    for (int i = 0; i < cJSON_GetArraySize(lists); i++)
        if (!strcasecmp(store_media_list_name(i), name)) return i;
    if (cJSON_GetArraySize(lists) >= 8) return -1;
    cJSON *list = cJSON_CreateObject();
    if (!list) return -1;
    cJSON_AddStringToObject(list, "name", name);
    cJSON_AddItemToObject(list, "items", cJSON_CreateArray());
    cJSON_AddItemToArray(lists, list);
    save_media_lists();
    return cJSON_GetArraySize(lists) - 1;
}

int store_media_list_rename(int list_index, const char *name) {
    cJSON *list = media_list_at(list_index);
    if (!list || !name || !name[0]) return -1;
    cJSON_DeleteItemFromObjectCaseSensitive(list, "name");
    cJSON_AddStringToObject(list, "name", name);
    save_media_lists();
    return 0;
}

int store_media_list_delete(int list_index) {
    cJSON *lists = media_lists_array();
    if (!cJSON_IsArray(lists) || list_index < 0 || list_index >= cJSON_GetArraySize(lists)) return -1;
    cJSON_DeleteItemFromArray(lists, list_index);
    save_media_lists();
    return 0;
}

static cJSON *media_items(int list_index) {
    cJSON *items = cJSON_GetObjectItemCaseSensitive(media_list_at(list_index), "items");
    return cJSON_IsArray(items) ? items : NULL;
}

int store_media_list_item_count(int list_index) { return cJSON_GetArraySize(media_items(list_index)); }

int store_media_list_get(int list_index, int item_index, int *id, int *is_series,
                         char *title, size_t title_cap, char *logo, size_t logo_cap) {
    cJSON *item = cJSON_GetArrayItem(media_items(list_index), item_index);
    if (!item) return 0;
    cJSON *jid = cJSON_GetObjectItemCaseSensitive(item, "id");
    cJSON *series = cJSON_GetObjectItemCaseSensitive(item, "series");
    cJSON *jt = cJSON_GetObjectItemCaseSensitive(item, "title");
    cJSON *jl = cJSON_GetObjectItemCaseSensitive(item, "logo");
    if (id) *id = cJSON_IsNumber(jid) ? jid->valueint : 0;
    if (is_series) *is_series = cJSON_IsTrue(series) || (cJSON_IsNumber(series) && series->valueint != 0);
    if (title && title_cap) snprintf(title, title_cap, "%s", cJSON_IsString(jt) ? jt->valuestring : "Titulo");
    if (logo && logo_cap) snprintf(logo, logo_cap, "%s", cJSON_IsString(jl) ? jl->valuestring : "");
    return 1;
}

int store_media_list_add(int list_index, int id, int is_series, const char *title, const char *logo) {
    cJSON *items = media_items(list_index);
    if (!cJSON_IsArray(items) || id <= 0) return -1;
    for (int i = 0; i < cJSON_GetArraySize(items); i++) {
        cJSON *old = cJSON_GetArrayItem(items, i);
        cJSON *oid = cJSON_GetObjectItemCaseSensitive(old, "id");
        cJSON *os = cJSON_GetObjectItemCaseSensitive(old, "series");
        int old_series = cJSON_IsTrue(os) || (cJSON_IsNumber(os) && os->valueint != 0);
        if (cJSON_IsNumber(oid) && oid->valueint == id && old_series == !!is_series) return 1;
    }
    if (cJSON_GetArraySize(items) >= 64) return -2;
    cJSON *item = cJSON_CreateObject();
    if (!item) return -1;
    cJSON_AddNumberToObject(item, "id", id);
    cJSON_AddBoolToObject(item, "series", !!is_series);
    cJSON_AddStringToObject(item, "title", title ? title : "Titulo");
    cJSON_AddStringToObject(item, "logo", logo ? logo : "");
    cJSON_AddItemToArray(items, item);
    save_media_lists();
    return 0;
}

int store_media_list_remove(int list_index, int item_index) {
    cJSON *items = media_items(list_index);
    if (!cJSON_IsArray(items) || item_index < 0 || item_index >= cJSON_GetArraySize(items)) return -1;
    cJSON_DeleteItemFromArray(items, item_index);
    save_media_lists();
    return 0;
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

int store_load_device_id(char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    FILE *f = fopen(DEVICE_F, "rb");
    if (!f) return 0;
    size_t n = fread(out, 1, cap - 1, f); fclose(f); out[n] = '\0';
    while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' || out[n-1] == ' ' || out[n-1] == '\t')) out[--n] = '\0';
    return out[0] != '\0';
}
void store_save_device_id(const char *id) {
    if (!id || !id[0]) return;
    FILE *f = fopen(DEVICE_F, "wb");
    if (f) { fwrite(id, 1, strlen(id), f); fclose(f); }
}

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

int store_load_player_volume(int *volume) {
    int value;
    FILE *f;
    if (!volume) return 0;
    f = fopen(PREFV_F, "rb");
    if (!f) return 0;
    if (fscanf(f, "%d", &value) != 1) { fclose(f); return 0; }
    fclose(f);
    if (value < 0 || value > 100) return 0;
    *volume = value;
    return 1;
}

void store_save_player_volume(int volume) {
    FILE *f;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    f = fopen(PREFV_F, "wb");
    if (!f) return;
    fprintf(f, "%d", volume);
    fclose(f);
}

void store_save_player_stats(int width, int height, int decoded_frames,
                             int dropped_frames, int buffering_events,
                             unsigned max_audio_bytes, int playback_error,
                             int hardware_decode, int slow_reads,
                             unsigned worst_read_ms, int present_gaps,
                             unsigned worst_present_ms) {
    FILE *f = fopen(PLAYER_STATS_F, "wb");
    if (!f) return;
    fprintf(f, "resolution=%dx%d\nhardware_decode=%d\ndecoded_frames=%d\ndropped_frames=%d\n"
               "buffering_events=%d\nmax_audio_queue_bytes=%u\nerror=%d\n"
               "slow_reads=%d\nworst_read_ms=%u\npresent_gaps=%d\nworst_present_ms=%u\n",
            width, height, hardware_decode, decoded_frames, dropped_frames,
            buffering_events, max_audio_bytes, playback_error,
            slow_reads, worst_read_ms, present_gaps, worst_present_ms);
    fclose(f);
}

int store_load_player_stats(struct player_stats *out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(PLAYER_STATS_F, "rb");
    if (!f) return 0;
    char line[96];
    int found_resolution = 0, found_result = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "resolution=%dx%d", &out->width, &out->height) == 2) found_resolution = 1;
        else if (sscanf(line, "hardware_decode=%d", &out->hardware_decode) == 1) { }
        else if (sscanf(line, "decoded_frames=%d", &out->decoded_frames) == 1) { }
        else if (sscanf(line, "dropped_frames=%d", &out->dropped_frames) == 1) { }
        else if (sscanf(line, "buffering_events=%d", &out->buffering_events) == 1) { }
        else if (sscanf(line, "max_audio_queue_bytes=%u", &out->max_audio_bytes) == 1) { }
        else if (sscanf(line, "slow_reads=%d", &out->slow_reads) == 1) { }
        else if (sscanf(line, "worst_read_ms=%u", &out->worst_read_ms) == 1) { }
        else if (sscanf(line, "present_gaps=%d", &out->present_gaps) == 1) { }
        else if (sscanf(line, "worst_present_ms=%u", &out->worst_present_ms) == 1) { }
        else if (sscanf(line, "error=%d", &out->playback_error) == 1) found_result = 1;
    }
    fclose(f);
    return found_resolution && found_result;
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
