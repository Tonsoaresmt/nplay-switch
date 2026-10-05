#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "navigation_input.h"
#include "text_fit.h"
typedef struct { int x,y,w,h; } SDL_Rect;
typedef struct { int unused; } SDL_Texture;
static const char *TAB_NAME[] = {"Inicio","Filmes","Series","Animes","Sagas","Historico"};
#define NTABS 6
#define C_TEXT 0
static void *gRen;
static unsigned measurements;
static int text_measure(const char *s,int style,int *w,int *h) {
    (void)style; measurements++; *w=0; *h=30;
    for(const unsigned char *p=(const unsigned char *)s;*p;p++) if((*p&0xc0)!=0x80)*w+=10;
    return 0;
}
static SDL_Texture *text_cached(void *r,const char *s,int c,int st,int *w,int *h) {
    (void)r;(void)c;text_measure(s,st,w,h);return NULL;
}
#include "app_topbar.inc"
#include "app_text_fit.inc"
int main(void) {
    NavigationRepeat n={.direction=-1};
    navigation_repeat_press(&n,15,100);
    assert(navigation_repeat_poll(&n,15,100)==-1);
    assert(navigation_repeat_poll(&n,-1,101)==-1);
    navigation_repeat_press(&n,15,200);
    assert(navigation_repeat_poll(&n,15,579)==-1);
    assert(navigation_repeat_poll(&n,15,580)==15);
    assert(navigation_repeat_poll(&n,15,581)==-1);
    assert(navigation_repeat_poll(&n,15,635)==15);
    assert(navigation_repeat_poll(&n,15,5000)==15);
    assert(navigation_repeat_poll(&n,15,5000)==-1);
    navigation_repeat_block(&n);
    assert(navigation_repeat_poll(&n,15,9999)==-1);
    assert(navigation_repeat_poll(&n,13,9999)==-1);
    assert(navigation_repeat_poll(&n,-1,10000)==-1);
    assert(navigation_repeat_poll(&n,13,10001)==13);
    navigation_repeat_block(&n); navigation_repeat_press(&n,12,10002);
    assert(!n.blocked && n.direction==12);
    navigation_repeat_press(&n,14,UINT32_MAX-100);
    assert(navigation_repeat_poll(&n,14,278)==-1);
    assert(navigation_repeat_poll(&n,14,279)==14);
    puts("PASS navigation: quick taps, no duplicate, cadence, stall, neutral gate, tick wrap");
    topbar_layout();
    for(int i=0;i<NTABS;i++) {
        assert(topbar_contains(&g_topbar_tabs[i],g_topbar_tabs[i].x,33));
        assert(!topbar_contains(&g_topbar_tabs[i],g_topbar_tabs[i].x+g_topbar_tabs[i].w,33));
        assert(g_topbar_tabs[i].x+g_topbar_tabs[i].w < TOPBAR_SEARCH.x);
    }
    assert(topbar_contains(&TOPBAR_SEARCH,1026,33));
    assert(topbar_contains(&TOPBAR_SEARCH,1159,76));
    assert(!topbar_contains(&TOPBAR_SEARCH,1160,76));
    assert(topbar_contains(&TOPBAR_PROFILE,1184,12));
    assert(TOPBAR_SEARCH.x+TOPBAR_SEARCH.w < TOPBAR_PROFILE.x);
    puts("PASS extracted topbar: shared draw/touch geometry and separated targets");
    char out[128];
    assert(text_fit_build("Ação 日本語",out,sizeof(out),0,70,text_measure));
    assert(!strcmp(out,"Ação..."));
    assert(text_fit_build("Ação",out,sizeof(out),0,40,text_measure) && !strcmp(out,"Ação"));
    assert(text_fit_build("日本語",out,sizeof(out),0,10,text_measure) && !out[0]);
    assert(text_fit_build("😀😀😀😀😀",out,sizeof(out),0,40,text_measure) && !strcmp(out,"😀..."));
    assert(!text_fit_build("too long",out,4,0,40,text_measure));
    measurements=0;
    assert(!strcmp(text_fitted("Long title to fit",0,90),"Long t..."));
    unsigned before=measurements;
    for(int i=0;i<10000;i++)assert(!strcmp(text_fitted("Long title to fit",0,90),"Long t..."));
    assert(measurements==before);
    assert(!strcmp(text_fitted("Long title to fit",0,100),"Long ti..."));
    puts("PASS text: UTF-8 accents/Japanese/emoji, tiny widths, 10000 cache hits without remeasurement");
    puts("APP NAVIGATION PASS (host geometry/logic, not GPU or physical Switch)");
}
