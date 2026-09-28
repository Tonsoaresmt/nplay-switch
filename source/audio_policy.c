#include "audio_policy.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void ascii_lower(const char *value, char *out, size_t cap) {
    size_t j = 0;
    if (!out || cap == 0) return;
    if (!value) value = "";
    for (size_t i = 0; value[i] && j + 1 < cap; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < 128) out[j++] = (char)tolower(c);
        else if (c == 0xc3 && value[i + 1]) {
            // As palavras usadas pela politica continuam reconheciveis sem o
            // caractere acentuado (portugu[e]s, japon[e]s, ingl[e]s etc.).
            i++;
        }
    }
    out[j] = '\0';
}

static int contains(const char *text, const char *word) {
    return text && word && strstr(text, word) != NULL;
}

const char *audio_language_normalize(const char *language, const char *title) {
    static const struct { const char *code, *norm; } codes[] = {
        { "pt", "pt" }, { "por", "pt" }, { "pob", "pt" }, { "bra", "pt" }, { "br", "pt" },
        { "en", "en" }, { "eng", "en" },
        { "ja", "ja" }, { "jp", "ja" }, { "jpn", "ja" },
        { "es", "es" }, { "spa", "es" },
        { "fr", "fr" }, { "fre", "fr" }, { "fra", "fr" },
        { "it", "it" }, { "ita", "it" },
        { "de", "de" }, { "ger", "de" }, { "deu", "de" },
        { "ko", "ko" }, { "kor", "ko" },
        { "zh", "zh" }, { "chi", "zh" }, { "zho", "zh" },
        { "ru", "ru" }, { "rus", "ru" }
    };
    char lang[32], name[128];
    ascii_lower(language, lang, sizeof(lang));
    ascii_lower(title, name, sizeof(name));
    char base[8] = "";
    size_t n = 0;
    while (lang[n] && lang[n] != '-' && lang[n] != '_' && lang[n] != ' ' && n + 1 < sizeof(base)) {
        base[n] = lang[n];
        n++;
    }
    base[n] = '\0';
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
        if (!strcmp(base, codes[i].code)) return codes[i].norm;

    const char *joined[2] = { lang, name };
    for (int i = 0; i < 2; i++) {
        const char *s = joined[i];
        if (contains(s, "portugu") || contains(s, "brasil") || contains(s, "brazil") ||
            contains(s, "dublad") || contains(s, "nacional") || contains(s, "pt-br") ||
            contains(s, "pt_br")) return "pt";
        if (contains(s, "english") || contains(s, "ingles")) return "en";
        if (contains(s, "japan") || contains(s, "japon")) return "ja";
        if (contains(s, "spanish") || contains(s, "espanh") || contains(s, "castel") ||
            contains(s, "latino")) return "es";
        if (contains(s, "french") || contains(s, "franc")) return "fr";
        if (contains(s, "italian")) return "it";
        if (contains(s, "german") || contains(s, "alem")) return "de";
        if (contains(s, "korean") || contains(s, "corean")) return "ko";
        if (contains(s, "chinese") || contains(s, "chin")) return "zh";
        if (contains(s, "russian") || contains(s, "russo")) return "ru";
    }
    return "";
}

AudioLanguageKind audio_language_kind(const char *language, const char *title) {
    const char *norm = audio_language_normalize(language, title);
    if (!strcmp(norm, "pt")) return AUDIO_KIND_PORTUGUESE;
    if (!strcmp(norm, "en")) return AUDIO_KIND_ENGLISH;
    if (!strcmp(norm, "ja")) return AUDIO_KIND_JAPANESE;
    return norm[0] ? AUDIO_KIND_OTHER : AUDIO_KIND_UNKNOWN;
}

int audio_version_preference(const char *language, const char *label) {
    char raw[160];
    snprintf(raw, sizeof(raw), "%s %s", language ? language : "", label ? label : "");
    char lower[160];
    ascii_lower(raw, lower, sizeof(lower));
    if (contains(lower, "dual")) return -1;
    if (contains(lower, "legend") || contains(lower, "subtit") ||
        contains(lower, "subbed") || contains(lower, "original")) return 1;
    if (contains(lower, "dublad") ||
        audio_language_kind(language, label) == AUDIO_KIND_PORTUGUESE) return 0;
    return -1;
}

static int title_has(const AudioTrackInfo *track, const char *word) {
    char title[128];
    ascii_lower(track ? track->title : NULL, title, sizeof(title));
    return contains(title, word);
}

static int track_is_commentary(const AudioTrackInfo *track) {
    return track && (track->is_commentary || title_has(track, "comment") ||
           title_has(track, "director") || title_has(track, "descricao") ||
           title_has(track, "descritiv") || title_has(track, "audio description"));
}

static int safe_best(int best, int count) {
    return best >= 0 && best < count ? best : 0;
}

int audio_policy_choose(const AudioTrackInfo *tracks, int count, int account_pref,
                        const char *saved_language,
                        const char *continuity_language, int continuity_index,
                        int best_index) {
    if (!tracks || count <= 0) return -1;
    if (count > AUDIO_POLICY_MAX_TRACKS) count = AUDIO_POLICY_MAX_TRACKS;
    int best = safe_best(best_index, count);

    // A escolha feita no episodio anterior vence a preferencia geral da conta.
    // Isso preserva uma troca manual feita pelo usuario durante uma maratona.
    const char *continued = audio_language_normalize(continuity_language, NULL);
    if (continued[0]) {
        for (int i = 0; i < count; i++)
            if (!strcmp(audio_language_normalize(tracks[i].language, tracks[i].title), continued)) return i;
    } else if (continuity_language && (!strcmp(continuity_language, "und") ||
                                       !strcmp(continuity_language, "unknown"))) {
        for (int i = 0; i < count; i++)
            if (audio_language_kind(tracks[i].language, tracks[i].title) == AUDIO_KIND_UNKNOWN) return i;
    }
    if (continuity_index > 0 && continuity_index <= count) {
        int candidate = continuity_index - 1;
        AudioLanguageKind kind = audio_language_kind(tracks[candidate].language, tracks[candidate].title);
        // Indice e o ultimo recurso para pacotes inteiramente sem metadados.
        int all_unknown = 1;
        for (int i = 0; i < count; i++)
            if (audio_language_kind(tracks[i].language, tracks[i].title) != AUDIO_KIND_UNKNOWN) all_unknown = 0;
        if (all_unknown && kind == AUDIO_KIND_UNKNOWN) return candidate;
    }

    if (account_pref == 0) { // Dublado
        for (int i = 0; i < count; i++)
            if (!track_is_commentary(&tracks[i]) &&
                audio_language_kind(tracks[i].language, tracks[i].title) == AUDIO_KIND_PORTUGUESE) return i;

        // Contrato dos pacotes R2 antigos, igual ao site: ingles identificado
        // + segunda faixa sem tag significa que a faixa preservada e a dublagem.
        if (count == 2) {
            int english = -1, unknown = -1;
            for (int i = 0; i < count; i++) {
                AudioLanguageKind kind = audio_language_kind(tracks[i].language, tracks[i].title);
                if (kind == AUDIO_KIND_ENGLISH) english = i;
                if (kind == AUDIO_KIND_UNKNOWN && !track_is_commentary(&tracks[i])) unknown = i;
            }
            if (english >= 0 && unknown >= 0) return unknown;
        }
    } else if (account_pref == 1) { // Legendado / original
        for (int i = 0; i < count; i++)
            if (!track_is_commentary(&tracks[i]) &&
                (audio_language_kind(tracks[i].language, tracks[i].title) == AUDIO_KIND_JAPANESE ||
                 title_has(&tracks[i], "original"))) return i;
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < count; i++) {
                AudioLanguageKind kind = audio_language_kind(tracks[i].language, tracks[i].title);
                if (track_is_commentary(&tracks[i]) || kind == AUDIO_KIND_PORTUGUESE ||
                    kind == AUDIO_KIND_UNKNOWN) continue;
                if ((pass == 0) == !!tracks[i].is_default) return i;
            }
        }
        for (int i = 0; i < count; i++)
            if (!track_is_commentary(&tracks[i]) &&
                audio_language_kind(tracks[i].language, tracks[i].title) == AUDIO_KIND_UNKNOWN) return i;
    } else { // Tanto faz: respeita uma escolha manual salva neste perfil.
        const char *saved = audio_language_normalize(saved_language, NULL);
        if (saved[0]) {
            for (int i = 0; i < count; i++)
                if (!strcmp(audio_language_normalize(tracks[i].language, tracks[i].title), saved)) return i;
        }
    }

    if (!track_is_commentary(&tracks[best])) return best;
    for (int i = 0; i < count; i++) if (!track_is_commentary(&tracks[i])) return i;
    return best;
}
