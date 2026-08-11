// store.h - token de login + progresso/historico de leitura no SD.
#pragma once
#include <stddef.h>

void store_init(void);

int  store_load_token(char *out, size_t cap);
void store_save_token(const char *token);
void store_clear_token(void);

int  store_load_pref_audio(char *out, size_t cap);
void store_save_pref_audio(const char *lang);

int  store_load_pref_sub(char *out, size_t cap);
void store_save_pref_sub(const char *lang);

int  store_load_player_volume(int *volume);
void store_save_player_volume(int volume);
void store_save_player_stats(int width, int height, int decoded_frames,
                             int dropped_frames, int buffering_events,
                             unsigned max_audio_bytes, int playback_error);
struct player_stats {
    int width, height, decoded_frames, dropped_frames, buffering_events;
    unsigned max_audio_bytes;
    int playback_error;
};
int store_load_player_stats(struct player_stats *out);

// Listas pessoais locais do catalogo. Ficam no Switch e podem ser sincronizadas
// com a conta no futuro sem mudar a UI que as consome.
int  store_media_list_count(void);
const char *store_media_list_name(int list_index);
int  store_media_list_create(const char *name);
int  store_media_list_rename(int list_index, const char *name);
int  store_media_list_delete(int list_index);
int  store_media_list_item_count(int list_index);
int  store_media_list_get(int list_index, int item_index, int *id, int *is_series,
                          char *title, size_t title_cap, char *logo, size_t logo_cap);
int  store_media_list_add(int list_index, int id, int is_series,
                          const char *title, const char *logo);
int  store_media_list_remove(int list_index, int item_index);

int  store_load_server(char *out, size_t cap);
void store_save_server(const char *url);

int  store_load_user(char *out, size_t cap);
void store_save_user(const char *user);
void store_clear_user(void);

int  store_load_update_seen(char *out, size_t cap);
void store_save_update_seen(const char *tag);

int  store_load_local_root(char *out, size_t cap);
void store_save_local_root(const char *path);

int  store_load_last_area(char *out, size_t cap);
void store_save_last_area(const char *area);

int  store_load_area_hidden_mask(unsigned *mask);
void store_save_area_hidden_mask(unsigned mask);

// Orientacao da tela: 1 = retrato, 0 = paisagem. Persiste entre sessoes.
int  store_load_orientation(int *portrait);
void store_save_orientation(int portrait);

// Leitura de livros: 1 = modo noturno em PDF/EPUB, 0 = pagina original.
int  store_load_doc_night(int *enabled);
void store_save_doc_night(int enabled);

// Modo de ajuste da pagina por serie: 0 = Auto, 1 = Conter, 2 = Largura.
int  store_get_fit_mode(const char *seriesId, int fallback);
void store_set_fit_mode(const char *seriesId, int mode);

// Estado offline de uma serie: 0 = nada, 1 = parcial, 2 = baixada inteira.
int  store_get_series_offline(const char *seriesId);
void store_set_series_offline(const char *seriesId, int state);
void store_clear_series_offline_all(void);

// pagina salva de um capitulo (>=1) ou 1 se nao houver.
int  store_get_progress(const char *bookId);

// Preferencia visual de livros/PDF/EPUB por item. fallback normalmente = M.
int  store_get_doc_scale(const char *bookId, int fallback);
void store_set_doc_scale(const char *bookId, int scale);

// registra/atualiza a leitura (pagina atual + metadados p/ "continuar lendo").
void store_record(const char *bookId, int page, const char *seriesId,
                  const char *seriesTitle, const char *chapLabel,
                  const char *pageBase, const char *seriesCover, int pages);

// preenche ids[] com ate `max` capitulos lidos recentemente (mais recente 1o).
// Retorna a quantidade.
int  store_recent(char ids[][96], int max);

// le os campos salvos de um capitulo. Retorna 1 se existe.
int  store_entry(const char *bookId,
                 char *seriesId, size_t sidCap,
                 char *seriesTitle, size_t stCap,
                 char *chapLabel, size_t clCap,
                 char *pageBase, size_t pbCap,
                 char *seriesCover, size_t cvCap,
                 int *page, int *pages);

void store_flush(void);
