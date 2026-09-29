#include "player_loading.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(player_loading_interrupt_can_draw(PLAYER_LOADING_OPENING,
                                              1, 0, 0, 180, 80, 80));
    assert(!player_loading_interrupt_can_draw(PLAYER_LOADING_OPENING,
                                               0, 0, 0, 180, 80, 80));
    assert(!player_loading_interrupt_can_draw(PLAYER_LOADING_OPENING,
                                               1, 1, 0, 180, 80, 80));
    assert(player_loading_interrupt_can_draw(PLAYER_LOADING_OPERATION,
                                              1, 1, 1, 180, 80, 80));

    // Regressao da 0.12.27: antes do primeiro quadro, o callback alternava
    // "Preparando video" com "Aguardando dados" do loop de reproducao.
    // Assim que o demux comeca, somente o loop principal pode apresentar.
    assert(!player_loading_interrupt_can_draw(PLAYER_LOADING_PLAYBACK,
                                               1, 0, 0, 180, 0, 80));
    assert(!player_loading_interrupt_can_draw(PLAYER_LOADING_PLAYBACK,
                                               1, 1, 1, 180, 0, 80));

    assert(!player_loading_interrupt_can_draw(PLAYER_LOADING_OPENING,
                                               1, 0, 0, 120, 80, 80));
    puts("player loading ownership: ok");
    return 0;
}
