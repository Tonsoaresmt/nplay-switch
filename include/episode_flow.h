#pragma once

#include "cJSON.h"

typedef struct {
    int found_current;
    int item_id;
    int series_id;
    int season_index;
    int episode_index;
    int flat_index;
} EpisodeNext;

// Follows the episode order returned by the series detail, including its next
// grouped season. A missing current item never guesses a replacement episode.
EpisodeNext episode_after(const cJSON *detail, int current_item_id);
EpisodeNext episode_first(const cJSON *detail);
int episode_coordinates_adjacent(int season, int episode,
                                 int next_season, int next_episode);
// Resolve a selecao da biblioteca pelo identificador da obra, nao pela
// posicao instavel retornada pelo polling dos jobs.
int episode_group_index(const int *keys, int count, int selected_key);
