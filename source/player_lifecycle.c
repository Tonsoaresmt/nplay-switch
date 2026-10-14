#include "player_lifecycle.h"
#include <SDL.h>
#include <stddef.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

static SDL_atomic_t resume_pending;
static SDL_atomic_t was_background;
#ifdef __SWITCH__
static AppletHookCookie lifecycle_cookie;
static int hook_registered;

static void lifecycle_hook(AppletHookType type, void *userdata) {
    (void)userdata;
    if (type == AppletHookType_OnResume) {
        SDL_AtomicSet(&was_background, 0);
        SDL_AtomicSet(&resume_pending, 1);
    } else if (type == AppletHookType_OnFocusState) {
        AppletFocusState focus = appletGetFocusState();
        if (focus == AppletFocusState_Background)
            SDL_AtomicSet(&was_background, 1);
        else if (focus == AppletFocusState_InFocus && SDL_AtomicGet(&was_background)) {
            SDL_AtomicSet(&was_background, 0);
            SDL_AtomicSet(&resume_pending, 1);
        }
        // OutOfFocus can be a keyboard/library applet, not HOME or sleep.
    }
}
#endif

void player_lifecycle_begin(void) {
    SDL_AtomicSet(&resume_pending, 0);
    SDL_AtomicSet(&was_background, 0);
#ifdef __SWITCH__
    if (!hook_registered) {
        appletHook(&lifecycle_cookie, lifecycle_hook, NULL);
        hook_registered = 1;
    }
#endif
}

void player_lifecycle_end(void) {
#ifdef __SWITCH__
    if (hook_registered) {
        appletUnhook(&lifecycle_cookie);
        hook_registered = 0;
    }
#endif
    player_lifecycle_acknowledge();
}

int player_lifecycle_pending(void) { return SDL_AtomicGet(&resume_pending); }

void player_lifecycle_acknowledge(void) {
    SDL_AtomicSet(&resume_pending, 0);
}
