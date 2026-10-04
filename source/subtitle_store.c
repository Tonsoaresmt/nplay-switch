#include "subtitle_store.h"
#include <stdlib.h>
#include <string.h>

#define MERGE_GAP 0.05f
#define MERGE_LOOKBACK 12
#define MAX_CANDIDATES 16
#define MAX_FRAGMENTS 8
#define MAX_LINES 4
#define CHARS_PER_LINE 48

int subtitle_text_is_drawing(const char *text) {
    if (!text) return 0;
    const char *p = text;
    while (*p == ' ' || *p == '\t') p++;
    if (p[0] != 'm' || p[1] != ' ') return 0;
    int numbers = 0;
    for (; *p; p++) {
        if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '.') {
            numbers++;
            while ((p[1] >= '0' && p[1] <= '9') || p[1] == '.') p++;
        } else if (!strchr(" mnlbspc\t\r\n", *p)) {
            return 0;
        }
    }
    return numbers >= 4;
}

static int codepoints(const char *text, size_t len) {
    int count = 0;
    for (size_t i = 0; i < len; i++)
        if (((unsigned char)text[i] & 0xC0) != 0x80 && text[i] != ' ' && text[i] != '\n') count++;
    return count;
}

static int grow(void **items, int *capacity, size_t size, int needed) {
    if (needed <= *capacity) return 1;
    int next = *capacity ? *capacity * 2 : 256;
    while (next < needed) next *= 2;
    void *grown = realloc(*items, (size_t)next * size);
    if (!grown) return 0;
    *items = grown;
    *capacity = next;
    return 1;
}

static int same_text(const SubtitleStore *store, const SubtitleStoreCue *cue,
                     const char *text, size_t len) {
    return cue->len == len && !memcmp(store->text + cue->text, text, len);
}

// Karaoke quadro a quadro e camadas repetidas: o mesmo texto encostado ou
// sobreposto vira um unico cue (no host, dezenas de milhares viram poucos).
static int merge_recent(SubtitleStore *store, SubtitleStoreCue *list, int count,
                        float start, float end, const char *text, size_t len, int is_long) {
    int stop = count > MERGE_LOOKBACK ? count - MERGE_LOOKBACK : 0;
    for (int i = count - 1; i >= stop; i--) {
        SubtitleStoreCue *cue = &list[i];
        if (start < cue->start - 0.001f || start > cue->end + MERGE_GAP ||
            !same_text(store, cue, text, len)) continue;
        float merged_end = end > cue->end ? end : cue->end;
        if (!is_long && merged_end - cue->start > SUBTITLE_STORE_LONG_SECONDS) return 0;
        cue->end = merged_end;
        if (!is_long && merged_end - cue->start > store->max_short)
            store->max_short = merged_end - cue->start;
        store->merged++;
        return 1;
    }
    return 0;
}

int subtitle_store_add(SubtitleStore *store, double start, double end, const char *text) {
    if (!store) return 0;
    if (!text) { store->skipped++; return 1; }
    while (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t') text++;
    size_t len = strlen(text);
    while (len && (text[len - 1] == ' ' || text[len - 1] == '\n' ||
                   text[len - 1] == '\r' || text[len - 1] == '\t')) len--;
    if (!len || subtitle_text_is_drawing(text)) { store->skipped++; return 1; }
    if (len > SUBTITLE_TEXT_CAP - 1) {
        len = SUBTITLE_TEXT_CAP - 1;
        while (len && ((unsigned char)text[len] & 0xC0) == 0x80) len--; // UTF-8 inteiro
    }
    if (start < 0) start = 0;
    if (!(end > start)) end = start + 4.0;
    float s = (float)start, e = (float)end;
    int is_long = end - start > SUBTITLE_STORE_LONG_SECONDS;
    if (is_long ? merge_recent(store, store->longs, store->long_count, s, e, text, len, 1)
                : merge_recent(store, store->cues, store->count, s, e, text, len, 0)) return 1;
    if (store->count + store->long_count >= SUBTITLE_STORE_MAX_CUES ||
        store->text_len + len + 1 > SUBTITLE_STORE_MAX_TEXT) {
        store->truncated++;
        return 0;
    }
    if (store->text_len + len + 1 > store->text_cap) {
        size_t next = store->text_cap ? store->text_cap * 2 : 16384;
        while (next < store->text_len + len + 1) next *= 2;
        char *grown = realloc(store->text, next);
        if (!grown) return 0;
        store->text = grown;
        store->text_cap = next;
    }
    SubtitleStoreCue cue = { s, e, (uint32_t)store->text_len, (uint16_t)len, 0 };
    memcpy(store->text + store->text_len, text, len);
    store->text[store->text_len + len] = 0;
    if (is_long) {
        if (!grow((void **)&store->longs, &store->long_capacity, sizeof(cue),
                  store->long_count + 1)) return 0;
        store->longs[store->long_count++] = cue;
    } else {
        if (!grow((void **)&store->cues, &store->capacity, sizeof(cue), store->count + 1))
            return 0;
        // Quase sempre chega em ordem; fora de ordem entra no lugar certo, pois
        // a busca depende da ordenacao (um cue adiantado escondia os seguintes).
        int at = store->count;
        if (at > 0 && store->cues[at - 1].start > s) {
            int lo = 0, hi = at;
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                if (store->cues[mid].start <= s) lo = mid + 1; else hi = mid;
            }
            // Reenvio (legenda do torrent pedida de novo desde o inicio): o
            // mesmo cue ja guardado e descartado em vez de duplicado.
            for (int k = lo - 1; k >= 0 && k >= lo - 32 && store->cues[k].start >= s - 0.002f; k--) {
                if (same_text(store, &store->cues[k], text, len) &&
                    store->cues[k].start <= s + 0.002f) {
                    if (e > store->cues[k].end) store->cues[k].end = e;
                    store->merged++;
                    return 1;
                }
            }
            memmove(&store->cues[lo + 1], &store->cues[lo], (size_t)(at - lo) * sizeof(cue));
            at = lo;
        }
        store->cues[at] = cue;
        store->count++;
        if (e - s > store->max_short) store->max_short = e - s;
    }
    store->text_len += len + 1;
    return 1;
}

int subtitle_store_count(const SubtitleStore *store) {
    return store ? store->count + store->long_count : 0;
}

int subtitle_store_get(const SubtitleStore *store, int index, double *start,
                       double *end, const char **text) {
    if (!store || index < 0 || index >= store->count + store->long_count) return 0;
    const SubtitleStoreCue *cue = index < store->count ? &store->cues[index]
                                                        : &store->longs[index - store->count];
    if (start) *start = cue->start;
    if (end) *end = cue->end;
    if (text) *text = store->text + cue->text;
    return 1;
}

typedef struct { const SubtitleStoreCue *cue; int lines; } Candidate;

static int cue_lines(const SubtitleStore *store, const SubtitleStoreCue *cue) {
    const char *text = store->text + cue->text;
    int lines = 1, run = 0;
    for (int i = 0; i < cue->len; i++) {
        if (text[i] == '\n') { lines++; run = 0; continue; }
        if (((unsigned char)text[i] & 0xC0) != 0x80 && ++run > CHARS_PER_LINE) { lines++; run = 0; }
    }
    return lines;
}

static void consider(const SubtitleStore *store, const SubtitleStoreCue *cue,
                     Candidate *talk, int *talk_n, Candidate *frags, int *frag_n) {
    for (int i = 0; i < *talk_n; i++)
        if (same_text(store, talk[i].cue, store->text + cue->text, cue->len)) return;
    for (int i = 0; i < *frag_n; i++)
        if (same_text(store, frags[i].cue, store->text + cue->text, cue->len)) return;
    Candidate candidate = { cue, cue_lines(store, cue) };
    if (codepoints(store->text + cue->text, cue->len) <= 2) {
        if (*frag_n < MAX_FRAGMENTS) frags[(*frag_n)++] = candidate;
    } else if (*talk_n < MAX_CANDIDATES) {
        talk[(*talk_n)++] = candidate;
    }
}

const char *subtitle_store_text(SubtitleStore *store, double position) {
    if (!store) return "";
    store->composed[0] = 0;
    float pos = (float)position;
    Candidate talk[MAX_CANDIDATES], frags[MAX_FRAGMENTS];
    int talk_n = 0, frag_n = 0;
    // Primeiro cue que comeca depois de pos; volta ate onde um curto ainda
    // poderia estar ativo (max_short). Placas longas sao poucas e varridas.
    int lo = 0, hi = store->count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (store->cues[mid].start <= pos + 0.05f) lo = mid + 1; else hi = mid;
    }
    for (int i = lo - 1; i >= 0; i--) {
        const SubtitleStoreCue *cue = &store->cues[i];
        if (cue->start < pos - store->max_short - 0.05f) break;
        if (pos < cue->end) consider(store, cue, talk, &talk_n, frags, &frag_n);
    }
    for (int i = 0; i < store->long_count; i++) {
        const SubtitleStoreCue *cue = &store->longs[i];
        if (cue->start <= pos + 0.05f && pos < cue->end)
            consider(store, cue, talk, &talk_n, frags, &frag_n);
    }
    // Karaoke (silabas soltas) so aparece quando nao disputa com uma fala.
    Candidate chosen[MAX_CANDIDATES];
    int chosen_n = 0;
    if (talk_n == 0 && frag_n <= 3) {
        for (int i = 0; i < frag_n; i++) chosen[chosen_n++] = frags[i];
    } else {
        // Prioridade para o cue mais curto (fala) dentro do limite de linhas;
        // placas longas ficam de fora quando nao cabe tudo.
        int used = 0;
        for (int taken = 0; taken < talk_n; taken++) {
            int best = -1;
            for (int i = 0; i < talk_n; i++) {
                if (!talk[i].cue) continue;
                float d = talk[i].cue->end - talk[i].cue->start;
                if (best < 0 || d < talk[best].cue->end - talk[best].cue->start) best = i;
            }
            if (best < 0) break;
            if (chosen_n == 0 || used + talk[best].lines <= MAX_LINES) {
                chosen[chosen_n++] = talk[best];
                used += talk[best].lines;
            }
            talk[best].cue = NULL;
        }
    }
    // Exibicao: placas longas em cima, falas por ordem de inicio embaixo.
    for (int i = 1; i < chosen_n; i++) {
        Candidate key = chosen[i];
        int key_sign = key.cue->end - key.cue->start > 10.0f;
        int j = i - 1;
        while (j >= 0) {
            int sign = chosen[j].cue->end - chosen[j].cue->start > 10.0f;
            if (sign > key_sign || (sign == key_sign && chosen[j].cue->start <= key.cue->start)) break;
            chosen[j + 1] = chosen[j];
            j--;
        }
        chosen[j + 1] = key;
    }
    size_t used = 0;
    for (int i = 0; i < chosen_n; i++) {
        const SubtitleStoreCue *cue = chosen[i].cue;
        size_t need = cue->len + (used ? 1 : 0);
        if (used + need + 1 > sizeof(store->composed)) break;
        if (used) store->composed[used++] = '\n';
        memcpy(store->composed + used, store->text + cue->text, cue->len);
        used += cue->len;
        store->composed[used] = 0;
    }
    return store->composed;
}

void subtitle_store_move(SubtitleStore *dst, SubtitleStore *src) {
    if (!dst || !src || dst == src) return;
    subtitle_store_free(dst);
    *dst = *src;
    memset(src, 0, sizeof(*src));
}

void subtitle_store_free(SubtitleStore *store) {
    if (!store) return;
    free(store->cues);
    free(store->longs);
    free(store->text);
    memset(store, 0, sizeof(*store));
}
