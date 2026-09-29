#ifndef NPLAY_PLAYER_SYNC_H
#define NPLAY_PLAYER_SYNC_H

#define PLAYER_SYNC_EXIT_GRACE_MS 1200u

// Evita repetir o mesmo POST no encerramento, mas preserva um ponto novo
// quando a reproducao realmente avancou desde o ultimo salvamento confirmado.
int player_sync_final_progress_needed(int presented_frame, int position_sec,
                                      int last_saved_position_sec);

// A saida do player nunca deve ficar presa ao timeout completo da rede.
int player_sync_exit_should_cancel(unsigned elapsed_ms, int worker_finished);

#endif
