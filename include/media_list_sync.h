#pragma once
#include "cJSON.h"
// Caller owns result. NULL means invalid input/OOM/capacity: retain local data.
cJSON *media_list_sync_merge(const cJSON *local, const cJSON *remote);
