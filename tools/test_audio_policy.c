#include "audio_policy.h"
#include <assert.h>
#include <stdio.h>

static AudioTrackInfo t(const char *lang, const char *title, int def) {
    AudioTrackInfo track = { lang, title, def, 0 };
    return track;
}

int main(void) {
    assert(audio_effective_preference(0, 1, 0) == 0);
    assert(audio_effective_preference(1, 0, 0) == 1);
    assert(audio_effective_preference(0, 1, 1) == 1);
    assert(audio_effective_preference(2, 0, 0) == 0);
    assert(audio_effective_preference(2, -1, 0) == 2);

    AudioTrackInfo dual[] = { t("eng", "English", 1), t("und", "Audio 2", 0) };
    assert(audio_policy_choose(dual, 2, 0, "en", NULL, 0, 0, 0) == 1);

    AudioTrackInfo tagged[] = { t("eng", "English", 1), t("pt-BR", "Portugues", 0) };
    assert(audio_policy_choose(tagged, 2, 0, "en", NULL, 0, 0, 0) == 1);
    assert(audio_policy_choose(tagged, 2, 1, NULL, NULL, 0, 0, 0) == 0);

    AudioTrackInfo title_only[] = { t("und", "English", 1), t("und", "Dublado Nacional", 0) };
    assert(audio_policy_choose(title_only, 2, 0, NULL, NULL, 0, 0, 0) == 1);
    AudioTrackInfo hls_name_in_comment[] = {
        t("und", "English", 1), t("und", "Portugues (Brasil)", 0)
    };
    assert(audio_policy_choose(hls_name_in_comment, 2, 0, NULL, NULL, 0, 0, 0) == 1);

    AudioTrackInfo anime[] = { t("por", "Dublado", 1), t("jpn", "Original", 0) };
    assert(audio_policy_choose(anime, 2, 1, NULL, NULL, 0, 0, 0) == 1);

    AudioTrackInfo anime_multi[] = {
        t("eng", "English", 1), t("spa", "Spanish", 0),
        t("jpn", "Japanese", 0), t("por", "Portugues Brasil", 0)
    };
    assert(audio_policy_choose(anime_multi, 4, 1, NULL, NULL, 0, 0, 0) == 2);
    assert(audio_policy_choose(anime_multi, 4, 0, NULL, NULL, 0, 0, 0) == 3);

    AudioTrackInfo commentary[] = { t("eng", "Director Commentary", 1), t("spa", "Espanol", 0) };
    assert(audio_policy_choose(commentary, 2, 1, NULL, NULL, 0, 0, 0) == 1);
    AudioTrackInfo old_commentary[] = { t("und", "Commentary", 1), t("eng", "English", 0) };
    assert(audio_policy_choose(old_commentary, 2, 0, NULL, NULL, 0, 0, 1) == 1);

    AudioTrackInfo foreign_only[] = { t("eng", "English", 1), t("spa", "Spanish", 0) };
    assert(audio_policy_choose(foreign_only, 2, 0, NULL, NULL, 0, 0, 0) == 0);

    AudioTrackInfo swapped[] = { t("pt", "Portugues", 0), t("eng", "English", 1) };
    // Uma pista herdada do episodio anterior nao pode vencer Dublado.
    assert(audio_policy_choose(swapped, 2, 0, NULL, "en", 1, 0, 0) == 0);
    // Na recuperacao da MESMA reproducao, a escolha manual continua valida.
    assert(audio_policy_choose(swapped, 2, 0, NULL, "en", 1, 1, 0) == 1);
    // Reabertura apos escolha manual preserva o indice, inclusive quando duas
    // faixas compartilham o mesmo idioma.
    AudioTrackInfo same_language[] = {
        t("pt-BR", "Dublado", 1), t("pt-BR", "Audiodescricao", 0)
    };
    assert(audio_policy_choose(same_language, 2, 0, NULL, "pt", 2, 2, 0) == 1);

    AudioTrackInfo unknown_swapped[] = { t("und", "Audio 1", 0), t("eng", "English", 1) };
    assert(audio_policy_choose(unknown_swapped, 2, 0, NULL, "und", 2, 0, 1) == 0);

    AudioTrackInfo any[] = { t("pt", "Portugues", 0), t("eng", "English", 1) };
    assert(audio_policy_choose(any, 2, 2, "pt", NULL, 0, 0, 1) == 0);
    assert(audio_policy_choose(any, 2, 2, NULL, NULL, 0, 0, 1) == 1);
    assert(audio_policy_choose(any, 2, 2, NULL, "en", 2, 0, 0) == 1);

    assert(audio_language_kind("pob", NULL) == AUDIO_KIND_PORTUGUESE);
    assert(audio_language_kind("und", "Portugu\xc3\xaas Brasil") == AUDIO_KIND_PORTUGUESE);
    assert(audio_version_preference("pt-BR", "Dublado") == 0);
    assert(audio_version_preference("en", "Legendado") == 1);
    assert(audio_version_preference(NULL, "Audio original") == 1);
    assert(audio_version_preference(NULL, "Dual audio") == -1);
    puts("audio policy: ok");
    return 0;
}
