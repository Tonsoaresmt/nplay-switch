#include "episode_flow.h"
#include "audio_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cJSON *g_ser, *api_detail;
static int g_running = 1, g_series_audio_explicit, g_next_audio_pref_override;
static int g_next_audio_pref_explicit, g_next_audio_hint, g_pref_audio, g_last_audio_index;
static char g_next_audio_language[8], g_last_audio_language[8];
void *gRen;
static int cancelled_request, requests, plays, jump, expected_next;
static int g_playback_chain, landing_reloads, chain_seen_during_play;
static int jint(const cJSON *j, const char *key) {
    const cJSON *v = cJSON_GetObjectItem(j, key); return cJSON_IsNumber(v) ? v->valueint : 0;
}
static const char *jstr(const cJSON *j, const char *key) {
    const cJSON *v = cJSON_GetObjectItem(j, key); return cJSON_IsString(v) ? v->valuestring : NULL;
}
static cJSON *ser_obj(void) { return cJSON_GetObjectItem(g_ser, "series"); }
static cJSON *ser_audio(void) { return cJSON_GetObjectItem(ser_obj(), "audio_versions"); }
// Legacy baseline uses the visible season; current code must not depend on it.
int ser_nep(void) { return cJSON_GetArraySize(cJSON_GetArrayItem(cJSON_GetObjectItem(g_ser, "seasons"), 0)); }
cJSON *ser_ep_at(int i) { return cJSON_GetArrayItem(cJSON_GetArrayItem(cJSON_GetObjectItem(g_ser, "seasons"), 0), i); }
static const char *ep_display_title(cJSON *ep) { return jstr(ep, "title"); }
static cJSON *ui_request_get(void *ren, const char *path, int *running, int *cancelled) {
    (void)ren; (void)running; assert(!strcmp(path, "/api/catalog/series/10"));
    requests++; *cancelled = cancelled_request;
    return cancelled_request ? NULL : cJSON_Duplicate(api_detail, 1);
}
static int g_play_chosen_item, built_for, choose_target, played_ids[8];
static void play_episodes_build(int id) { built_for = id; }
static void play_episodes_clear(void) {}
static void select_series_resume_target(cJSON *detail) { assert(detail); }
static void rebuild_series_plot(void) {}
static int resolve_and_play_details(int id, const char *title, const char *subtitle,
                                    const char *overview, const char *next_title, int has_next) {
    (void)overview; assert(id > 0 && title);
    if (plays < 8) played_ids[plays] = id;
    plays++;
    if (g_playback_chain > 0) chain_seen_during_play++;
    // Painel Episodios: o player devolve o episodio escolhido (ex.: o anterior).
    if (choose_target && plays == 1) { g_play_chosen_item = choose_target; return 2; }
    if (plays == 1) {
        assert(has_next == expected_next);
        if (has_next) assert(next_title && next_title[0]);
    }
    assert(subtitle);
    return jump && plays == 1 ? 2 : 0;
}
static int choose_next_episode(int sid, int current, int first, int refresh, int explicit_next,
                               char *title, size_t cap) {
    (void)sid; (void)first; (void)refresh; assert(explicit_next);
    EpisodeNext next = episode_after(g_ser, current);
    const cJSON *ep = episode_find(g_ser, next.item_id);
    if (!ep) return 0;
    snprintf(title, cap, "%s", jstr(ep, "title")); return next.item_id;
}
// Wrapper 0.12.46: a Home so recarrega quando a sequencia inteira termina.
static void playback_memory_leave(void) { if (g_playback_chain == 0) landing_reloads++; }
#include "episode_sequence.inc"

static void reset(void) {
    if (g_ser) cJSON_Delete(g_ser);
    g_ser = NULL; plays = requests = cancelled_request = jump = choose_target = built_for = 0;
    g_series_audio_explicit = 0; g_running = 1;
}
int main(void) {
    api_detail = cJSON_Parse("{\"series\":{\"id\":10,\"title\":\"Anime\"},\"seasons\":{"
        "\"2\":[{\"id\":201,\"season\":2,\"episode\":1,\"title\":\"Ep 3\"}],"
        "\"1\":[{\"id\":101,\"season\":1,\"episode\":1,\"title\":\"Ep 1\"},"
        "{\"id\":102,\"season\":1,\"episode\":2,\"title\":\"Ep 2\"}]}}");
    assert(api_detail);
    // Cached detail: actual chronological order, not the visible season.
    g_ser = cJSON_Duplicate(api_detail, 1); expected_next = 1; jump = 1;
    play_episode_sequence(101, 10, "Anime", NULL);
    assert(plays == 2 && !requests);
    // Dois episodios seguidos: nenhum recarregamento da Home entre eles, um so
    // ao final da sequencia (antes cada episodio disparava o catalogo).
    assert(chain_seen_during_play == 2 && landing_reloads == 1 && g_playback_chain == 0);
    reset(); g_ser = cJSON_Duplicate(api_detail, 1); expected_next = 1;
    play_episode_sequence(102, 10, "Anime", NULL); assert(plays == 1); // next season
    reset(); g_ser = cJSON_Duplicate(api_detail, 1); expected_next = 0;
    play_episode_sequence(201, 10, "Anime", NULL); assert(plays == 1); // finale
    reset(); expected_next = 1;
    play_episode_sequence(101, 10, "Anime", NULL); assert(requests == 1 && plays == 1);
    reset(); cancelled_request = 1;
    play_episode_sequence(101, 10, "Anime", NULL); assert(requests == 1 && !plays);
    reset(); g_ser = cJSON_Duplicate(api_detail, 1); expected_next = 1;
    cJSON *groups = cJSON_AddArrayToObject(ser_obj(), "season_group");
    cJSON *group = cJSON_CreateObject(); cJSON_AddNumberToObject(group, "id", 10);
    cJSON_AddItemToArray(groups, group);
    group = cJSON_CreateObject(); cJSON_AddNumberToObject(group, "id", 11);
    cJSON_AddItemToArray(groups, group);
    play_episode_sequence(201, 10, "Anime", NULL); assert(plays == 1);
    // Escolha direta no painel: volta ao episodio anterior sem pedir o "proximo".
    reset(); g_ser = cJSON_Duplicate(api_detail, 1); expected_next = 1; choose_target = 101;
    play_episode_sequence(102, 10, "Anime", NULL);
    assert(plays == 2 && played_ids[0] == 102 && played_ids[1] == 101 && built_for == 101);
    reset(); cJSON_Delete(api_detail);
    puts("actual episode sequence OK: anime, direct context, cancel, chronological next, season boundary, grouped season, finale, explicit jump, episodes panel choice, Home reload deferred to chain end");
}
