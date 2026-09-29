// audio_policy.h - selecao deterministica e testavel de faixas de audio.
#pragma once

#define AUDIO_POLICY_MAX_TRACKS 16

typedef enum {
    AUDIO_KIND_UNKNOWN = 0,
    AUDIO_KIND_PORTUGUESE,
    AUDIO_KIND_ENGLISH,
    AUDIO_KIND_JAPANESE,
    AUDIO_KIND_OTHER
} AudioLanguageKind;

typedef struct {
    const char *language;
    const char *title;
    int is_default;
    int is_commentary;
} AudioTrackInfo;

// Retorna um identificador curto estatico (pt/en/ja/es/...) ou "".
const char *audio_language_normalize(const char *language, const char *title);
AudioLanguageKind audio_language_kind(const char *language, const char *title);
// Preferencia declarada por uma versao de serie: 0=dub, 1=leg, -1=dual/indefinida.
int audio_version_preference(const char *language, const char *label);

// account_pref: 0=dublado, 1=legendado/original, 2=tanto faz.
// continuity_language/index descrevem a faixa escolhida no episodio anterior;
// continuity_index e 1-based. continuity_priority so deve ser 1 ao reabrir a
// mesma reproducao depois de uma falha; em outro episodio a conta vence.
// best_index e 0-based (FFmpeg).
int audio_policy_choose(const AudioTrackInfo *tracks, int count, int account_pref,
                        const char *saved_language,
                        const char *continuity_language, int continuity_index,
                        int continuity_priority, int best_index);
