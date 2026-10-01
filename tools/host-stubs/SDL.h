#pragma once
typedef unsigned int Uint32;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Thread SDL_Thread;
typedef struct { unsigned char r, g, b, a; } SDL_Color;
typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct { unsigned type; struct { unsigned char button; } jbutton;
    struct { float x, y; } tfinger; } SDL_Event;
#define SDL_QUIT 1
#define SDL_JOYBUTTONDOWN 2
#define SDL_FINGERDOWN 3
typedef struct { int value; } SDL_atomic_t;
Uint32 SDL_GetTicks(void);
int SDL_AtomicSet(SDL_atomic_t *, int);
int SDL_AtomicGet(SDL_atomic_t *);
SDL_Thread *SDL_CreateThread(int (*)(void *), const char *, void *);
void SDL_WaitThread(SDL_Thread *, int *);
int SDL_PollEvent(SDL_Event *);
int SDL_PushEvent(SDL_Event *);
void SDL_Delay(Uint32);
int SDL_SetRenderDrawColor(SDL_Renderer *, unsigned char, unsigned char, unsigned char, unsigned char);
int SDL_RenderClear(SDL_Renderer *);
void SDL_RenderPresent(SDL_Renderer *);
