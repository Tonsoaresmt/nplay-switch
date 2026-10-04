#include "subtitle_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int lines_of(const char *text) {
    if (!text[0]) return 0;
    int n = 1;
    for (const char *p = text; *p; p++) if (*p == '\n') n++;
    return n;
}

int main(void) {
    SubtitleStore store = {0};
    char text[64];
    // Anime com karaoke: falas a cada 5 s e 45 mil silabas entre 20 e 110 s.
    // O armazenamento antigo parava em 8192 cues e nenhuma fala depois disso.
    // Kana pseudoaleatorio (como karaoke real): quase nada se funde.
    static const char *kana[] = { "あ","い","う","え","お","か","き","く","け","こ","さ","し","す",
        "せ","そ","た","ち","つ","て","と","な","に","ぬ","ね","の","は","ひ","ふ","へ","ほ",
        "ま","み","む","め","も","や","ゆ","よ","ら","り","る","れ","ろ","わ","を","ん" };
    unsigned seed = 7;
    for (int t = 0; t <= 355; t += 5) {
        for (int i = (t - 20) * 100; t >= 20 && i < (t - 15) * 100 && i < 9000; i++)
            for (int layer = 0; layer < 5; layer++) {
                seed = seed * 1103515245u + 12345u;
                assert(subtitle_store_add(&store, 20 + i * 0.01, 20 + i * 0.01 + 0.3,
                                          kana[(seed >> 16) % 46]));
            }
        snprintf(text, sizeof(text), "Fala %d (%ds)", t / 5 + 1, t);
        assert(subtitle_store_add(&store, t + 0.5, t + 3.5, text));
    }
    assert(subtitle_store_count(&store) > 8192 && !store.truncated);
    assert(!strcmp(subtitle_store_text(&store, 151.0), "Fala 31 (150s)"));
    // Durante o karaoke a fala vence as silabas soltas.
    assert(!strcmp(subtitle_store_text(&store, 51.0), "Fala 11 (50s)"));
    // Karaoke sozinho com muitas silabas nao vira lixo na tela.
    assert(!strcmp(subtitle_store_text(&store, 54.2), ""));
    assert(!strcmp(subtitle_store_text(&store, 359.0), ""));
    size_t bytes = (size_t)store.capacity * sizeof(SubtitleStoreCue) + store.text_cap;
    assert(bytes < 2u * 1024u * 1024u); // antes: 45 mil x 528 bytes = 23 MB
    printf("karaoke: %d cues, %zu KB\n", subtitle_store_count(&store), bytes / 1024);
    subtitle_store_free(&store);

    // Placas longas sobrepostas nao empurram a fala para fora das 4 linhas.
    for (int k = 0; k < 6; k++) {
        snprintf(text, sizeof(text), "SIGN %c placa da cena", 'A' + k);
        assert(subtitle_store_add(&store, k * 0.2, 30, text));
    }
    assert(subtitle_store_add(&store, 10.5, 13.5, "Line 3 (10s)"));
    const char *shown = subtitle_store_text(&store, 11.0);
    assert(strstr(shown, "Line 3 (10s)") && lines_of(shown) <= 4);
    // A fala fica embaixo (ultima linha), as placas em cima.
    assert(!strcmp(strrchr(shown, '\n') + 1, "Line 3 (10s)"));
    subtitle_store_free(&store);

    // Dois personagens falando juntos: as duas falas, em ordem de inicio.
    assert(subtitle_store_add(&store, 1.0, 4.0, "- Vamos?"));
    assert(subtitle_store_add(&store, 1.5, 4.0, "- Agora!"));
    assert(!strcmp(subtitle_store_text(&store, 2.0), "- Vamos?\n- Agora!"));
    subtitle_store_free(&store);

    // Fora de ordem: um cue adiantado nao pode esconder os seguintes.
    assert(subtitle_store_add(&store, 100, 103, "depois"));
    assert(subtitle_store_add(&store, 50, 53, "antes"));
    assert(!strcmp(subtitle_store_text(&store, 51), "antes"));
    assert(!strcmp(subtitle_store_text(&store, 101), "depois"));
    subtitle_store_free(&store);

    // Torrent pedido de novo depois de uma queda: o reenvio nao duplica.
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < 100; i++) {
            snprintf(text, sizeof(text), "Linha %d", i);
            assert(subtitle_store_add(&store, i * 3.0, i * 3.0 + 2.0, text));
        }
    assert(subtitle_store_count(&store) == 100);
    assert(!strcmp(subtitle_store_text(&store, 151.0), "Linha 50"));
    subtitle_store_free(&store);

    // Camadas e quadros identicos viram um unico cue.
    for (int i = 0; i < 1000; i++) {
        assert(subtitle_store_add(&store, i * 0.04, i * 0.04 + 0.04, "Titulo do episodio"));
        assert(subtitle_store_add(&store, i * 0.04, i * 0.04 + 0.04, "Titulo do episodio"));
    }
    assert(subtitle_store_count(&store) == 1 && store.merged == 1999);
    assert(!strcmp(subtitle_store_text(&store, 39.0), "Titulo do episodio"));
    subtitle_store_free(&store);

    // Desenho vetorial do ASS e texto vazio sao ignorados.
    assert(subtitle_text_is_drawing("m 0 0 l 1280 0 1280 720 0 720"));
    assert(subtitle_text_is_drawing("  m -10.5 4 b 3 4 5 6 7 8"));
    assert(!subtitle_text_is_drawing("m e dá isso"));
    assert(!subtitle_text_is_drawing("Fala normal"));
    assert(subtitle_store_add(&store, 0, 10, "m 0 0 l 1280 0 1280 720 0 720"));
    assert(subtitle_store_add(&store, 0, 10, "  \n "));
    assert(subtitle_store_count(&store) == 0 && store.skipped == 2);
    // Placa de mais de 60 s (lista propria) e cue sem fim valido.
    assert(subtitle_store_add(&store, 0, 95, "Musica: abertura"));
    assert(subtitle_store_add(&store, 200, 0, "sem fim"));
    assert(!strcmp(subtitle_store_text(&store, 80), "Musica: abertura"));
    assert(!strcmp(subtitle_store_text(&store, 203), "sem fim"));
    // Iteracao (copia progressiva do torrent).
    double s, e; const char *t;
    assert(subtitle_store_get(&store, 1, &s, &e, &t) && !strcmp(t, "Musica: abertura") && e == 95);
    assert(!subtitle_store_get(&store, 2, &s, &e, &t));
    // Texto longo e cortado sem partir um caractere UTF-8.
    char big[700];
    for (int i = 0; i < 699; i += 2) { big[i] = (char)0xC3; big[i + 1] = (char)0xA9; }
    big[699] = 0;
    assert(subtitle_store_add(&store, 300, 302, big));
    const char *cut = subtitle_store_text(&store, 301);
    assert(strlen(cut) <= SUBTITLE_TEXT_CAP - 1 && strlen(cut) % 2 == 0);
    SubtitleStore moved = {0};
    subtitle_store_move(&moved, &store);
    assert(subtitle_store_count(&store) == 0 && subtitle_store_count(&moved) == 3);
    subtitle_store_free(&moved);
    puts("subtitle store: ok");
    return 0;
}
