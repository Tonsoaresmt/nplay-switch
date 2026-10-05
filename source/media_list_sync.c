#include "media_list_sync.h"
#include <math.h>
#include <string.h>
static int number_id(const cJSON *v) {
    return cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble > 0 &&
        v->valuedouble <= 2147483647.0 && v->valuedouble == v->valueint ? v->valueint : 0;
}
static int series(const cJSON *item) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(item, "series");
    return cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valueint != 0);
}
static int contains(const cJSON *items, int id, int kind) {
    const cJSON *item;
    cJSON_ArrayForEach(item, items)
        if (number_id(cJSON_GetObjectItemCaseSensitive(item, "id")) == id && series(item) == kind) return 1;
    return 0;
}
cJSON *media_list_sync_merge(const cJSON *local, const cJSON *remote) {
    if (!cJSON_IsArray(local) || !cJSON_IsArray(remote) || cJSON_GetArraySize(remote) > 64) return NULL;
    cJSON *result = cJSON_CreateArray();
    if (!result) return NULL;
    const cJSON *item;
    cJSON_ArrayForEach(item, remote) {
        int sid = number_id(cJSON_GetObjectItemCaseSensitive(item, "series_id"));
        int id = sid ? sid : number_id(cJSON_GetObjectItemCaseSensitive(item, "item_id"));
        const cJSON *title = cJSON_GetObjectItemCaseSensitive(item, "title");
        const cJSON *logo = cJSON_GetObjectItemCaseSensitive(item, "logo");
        if (!id || !cJSON_IsObject(item)) goto fail;
        if ((cJSON_IsString(title) && strlen(title->valuestring) > 512) ||
            (cJSON_IsString(logo) && strlen(logo->valuestring) > 2048)) goto fail;
        if (contains(result, id, sid > 0)) continue;
        cJSON *copy = cJSON_CreateObject();
        if (!copy) goto fail;
        if (!cJSON_AddNumberToObject(copy, "id", id) ||
            !cJSON_AddBoolToObject(copy, "series", sid > 0) ||
            !cJSON_AddStringToObject(copy, "title", cJSON_IsString(title) ? title->valuestring : "Titulo") ||
            !cJSON_AddStringToObject(copy, "logo", cJSON_IsString(logo) ? logo->valuestring : "") ||
            !cJSON_AddBoolToObject(copy, "synced", 1) || !cJSON_AddItemToArray(result, copy)) {
            cJSON_Delete(copy); goto fail;
        }
    }
    cJSON_ArrayForEach(item, local) {
        int id = number_id(cJSON_GetObjectItemCaseSensitive(item, "id"));
        if (!id) goto fail;
        // Unknown legacy provenance is conservative: do not erase an offline
        // addition merely because it is absent in this server snapshot.
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "synced")) || contains(result, id, series(item))) continue;
        if (cJSON_GetArraySize(result) >= 64) goto fail;
        cJSON *copy = cJSON_Duplicate(item, 1);
        if (!copy || !cJSON_AddItemToArray(result, copy)) { cJSON_Delete(copy); goto fail; }
    }
    return result;
fail:
    cJSON_Delete(result); return NULL;
}
