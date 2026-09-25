#include "episode_flow.h"

#include <assert.h>
#include <stdio.h>

static cJSON *fixture(const char *text) {
    cJSON *json = cJSON_Parse(text);
    assert(json);
    return json;
}

int main(void) {
    cJSON *series = fixture(
        "{\"series\":{\"id\":10,\"season_group\":[{\"id\":10},{\"id\":11}]},"
        "\"seasons\":{\"2\":[{\"id\":201,\"season\":2,\"episode\":1},"
        "{\"id\":202,\"season\":2,\"episode\":2}],"
        "\"1\":[{\"id\":101,\"season\":1,\"episode\":1},"
        "{\"id\":102,\"season\":1,\"episode\":2}]}}"
    );
    EpisodeNext next = episode_first(series);
    assert(next.item_id == 101 && next.season_index == 1 && next.episode_index == 0);
    next = episode_after(series, 101);
    assert(next.found_current && next.item_id == 102 && next.flat_index == 3);
    next = episode_after(series, 102);
    assert(next.item_id == 201 && next.season_index == 0);
    next = episode_after(series, 202);
    assert(next.found_current && !next.item_id && next.series_id == 11);
    next = episode_after(series, 999);
    assert(!next.found_current && !next.item_id && next.series_id == 10);
    cJSON_Delete(series);

    series = fixture("{\"series\":{\"id\":11},\"seasons\":{\"3\":[{\"id\":301,\"season\":3,\"episode\":1}]}}");
    next = episode_first(series);
    assert(next.item_id == 301 && next.series_id == 11);
    next = episode_after(series, 301);
    assert(next.found_current && !next.item_id && next.series_id == 11);
    cJSON_Delete(series);
    assert(episode_coordinates_adjacent(1, 1, 1, 2));
    assert(episode_coordinates_adjacent(1, 8, 2, 1));
    assert(!episode_coordinates_adjacent(1, 1, 1, 3));
    assert(!episode_coordinates_adjacent(0, 0, 1, 1));
    series = fixture("{\"series\":{\"id\":20},\"seasons\":{"
                     "\"1\":[{\"id\":211,\"season\":1,\"episode\":1}],"
                     "\"0\":[{\"id\":200,\"season\":0,\"episode\":1}]}}");
    next = episode_first(series);
    assert(next.item_id == 200);
    next = episode_after(series, 200);
    assert(next.item_id == 211);
    cJSON_Delete(series);
    puts("episode flow OK: same season, next season, grouped season, specials, gap, final, unknown");
    return 0;
}
