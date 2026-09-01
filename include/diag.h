#pragma once

#include <stddef.h>

#define DIAG_LINE_CAP 192

void diag_init(void);
void diag_exit(void);

// O trace do player e reiniciado apenas quando uma nova reproducao comeca.
// Assim, depois de um crash, reiniciar o app e abrir Configuracoes preserva
// exatamente os ultimos breadcrumbs do processo que morreu.
void diag_player_begin(int item_id, int session_id, int source_id,
                       const char *container, const char *delivery);
void diag_player_event(const char *component, const char *event,
                       const char *format, ...);
void diag_player_finish(int result_code);

// Log leve de latencia da API. A query string e removida para nao gravar busca,
// token ou qualquer parametro privado.
void diag_network_event(const char *method, const char *path, long http_code,
                        unsigned elapsed_ms, size_t response_bytes);

int diag_read_player_tail(char lines[][DIAG_LINE_CAP], int max_lines);
int diag_read_network_tail(char lines[][DIAG_LINE_CAP], int max_lines);

