#pragma once

// Renderer owns begin/ack/end. The applet hook only changes atomic flags;
// never use network, audio, FFmpeg, logging or renderer calls in the hook.
void player_lifecycle_begin(void);
void player_lifecycle_end(void);
int player_lifecycle_pending(void);
void player_lifecycle_acknowledge(void);
