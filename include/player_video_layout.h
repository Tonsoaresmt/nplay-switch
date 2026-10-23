#pragma once
// SDL_Rect is supplied by SDL (or the host test). Respect non-square pixels
// without changing the bounded size of the upload texture.
static inline SDL_Rect player_video_rect(int width, int height, int sar_num, int sar_den) {
    SDL_Rect r = {0,0,0,0};
    if (width <= 0 || height <= 0) return r;
    double aspect = (double)width / height;
    if (sar_num > 0 && sar_den > 0) aspect *= (double)sar_num / sar_den;
    if (aspect > 1280.0 / 720.0) { r.w = 1280; r.h = (int)(1280.0 / aspect); }
    else { r.h = 720; r.w = (int)(720.0 * aspect); }
    if (r.w < 1) r.w = 1;
    if (r.h < 1) r.h = 1;
    r.x = (1280-r.w)/2; r.y = (720-r.h)/2;
    return r;
}
