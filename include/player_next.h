#pragma once

// Estado visual do cartao de proximo episodio. Mantido fora de player.c para
// que a janela de exibicao possa ser simulada no host sem SDL/FFmpeg.
typedef struct {
    int available;
    float alpha;
    float progress;
} PlayerNextUi;

void player_next_ui(int has_next, double position, double duration,
                    int selected, PlayerNextUi *out);
