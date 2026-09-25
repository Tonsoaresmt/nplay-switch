#include "episode_flow.h"

#include <limits.h>
#include <stdlib.h>

static int number(const cJSON *object, const char *key) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(value) ? value->valueint : 0;
}

static int episode_order(const cJSON *episode, const cJSON *season, int fallback) {
    int n = number(episode, "season");
    if (n > 0) return n;
    if (season && season->string) {
        char *end = NULL;
        long parsed = strtol(season->string, &end, 10);
        if (end && !*end && parsed >= 0 && parsed < INT_MAX) return (int)parsed;
    }
    return fallback;
}

static EpisodeNext find_episode(const cJSON *detail, int current_item_id, int first) {
    EpisodeNext out = {0};
    const cJSON *seasons = cJSON_GetObjectItemCaseSensitive(detail, "seasons");
    const cJSON *series = cJSON_GetObjectItemCaseSensitive(detail, "series");
    out.series_id = number(series, "id");
    if (!seasons || (!first && current_item_id <= 0)) return out;

    int current_season = 0, current_episode = 0;
    int season_index = 0;
    const cJSON *season;
    cJSON_ArrayForEach(season, seasons) {
        int episode_index = 0;
        const cJSON *episode;
        cJSON_ArrayForEach(episode, season) {
            if (number(episode, "id") == current_item_id && !first) {
                out.found_current = 1;
                current_season = episode_order(episode, season, season_index + 1);
                current_episode = number(episode, "episode");
                if (current_episode <= 0) current_episode = episode_index + 1;
            }
            episode_index++;
        }
        season_index++;
    }
    if (!first && !out.found_current) return out;

    int best_season = INT_MAX, best_episode = INT_MAX;
    season_index = 0;
    int flat_index = 0;
    cJSON_ArrayForEach(season, seasons) {
        int episode_index = 0;
        const cJSON *episode;
        cJSON_ArrayForEach(episode, season) {
            int id = number(episode, "id");
            int s = episode_order(episode, season, season_index + 1);
            int e = number(episode, "episode");
            if (e <= 0) e = episode_index + 1;
            if (id > 0 && (first || id != current_item_id) &&
                (first || s > current_season ||
                            (s == current_season && e > current_episode)) &&
                (s < best_season || (s == best_season && e < best_episode))) {
                best_season = s;
                best_episode = e;
                out.item_id = id;
                out.season_index = season_index;
                out.episode_index = episode_index;
                out.flat_index = flat_index;
            }
            episode_index++;
            flat_index++;
        }
        season_index++;
    }
    if (out.item_id || first) return out;

    const cJSON *groups = cJSON_GetObjectItemCaseSensitive(series, "season_group");
    int seen_current = 0;
    const cJSON *group;
    cJSON_ArrayForEach(group, groups) {
        int id = number(group, "id");
        if (seen_current && id > 0 && id != out.series_id) {
            out.series_id = id;
            return out;
        }
        if (id == out.series_id) seen_current = 1;
    }
    return out;
}

EpisodeNext episode_after(const cJSON *detail, int current_item_id) {
    return find_episode(detail, current_item_id, 0);
}

EpisodeNext episode_first(const cJSON *detail) {
    return find_episode(detail, 0, 1);
}

int episode_coordinates_adjacent(int season, int episode,
                                 int next_season, int next_episode) {
    if (season <= 0 || episode <= 0 || next_season <= 0 || next_episode <= 0) return 0;
    return (season == next_season && next_episode == episode + 1) ||
           (next_season == season + 1 && next_episode == 1);
}
