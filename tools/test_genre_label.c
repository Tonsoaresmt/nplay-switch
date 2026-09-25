#include "genre_label.h"
#include <assert.h>
#include <string.h>

int main(void) {
    char out[240];
    movie_genre_label("Filmes;Torrent;HDR Torrent;Comédia;Crime;Policial", out, sizeof(out));
    assert(strcmp(out, "Filme · Comédia · Crime · Policial") == 0);
    movie_genre_label("Filmes, WEB-DL, Ação, Suspense", out, sizeof(out));
    assert(strcmp(out, "Filme · Ação · Suspense") == 0);
    movie_genre_label(NULL, out, sizeof(out));
    assert(strcmp(out, "Filme") == 0);
    return 0;
}
