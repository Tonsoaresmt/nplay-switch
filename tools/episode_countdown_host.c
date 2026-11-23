#include <SDL.h>
#include "cJSON.h"
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define WIN_W 1280
#define WIN_H 720
typedef int32_t Sint32;
enum{JOY_A=0,JOY_B=1,JOY_MINUS=10};
static Uint32 ticks;
static int g_running=1,g_epSel,mode,emitted,frames;
static SDL_Renderer *gRen;
static const SDL_Color C_BG={0},C_TEXT={0},C_MUT={0},C_ACC={0},C_ACC2={0},C_CARD={0};
Uint32 SDL_GetTicks(void){return ticks;}
void SDL_Delay(Uint32 n){ticks+=n;assert(++frames<1000);}
int SDL_PollEvent(SDL_Event *e){
 if(!mode||emitted)return 0;
 memset(e,0,sizeof(*e));emitted=1;e->type=SDL_JOYBUTTONDOWN;
 e->jbutton.button=mode==1?JOY_B:mode==2?JOY_MINUS:JOY_A;
 if(mode==4){e->type=SDL_FINGERDOWN;e->tfinger.x=600.f/1280;e->tfinger.y=480.f/720;}
 if(mode==5)e->type=SDL_QUIT;
 return 1;
}
int SDL_SetRenderDrawColor(SDL_Renderer*r,unsigned char a,unsigned char b,unsigned char c,unsigned char d){(void)r;(void)a;(void)b;(void)c;(void)d;return 0;}
int SDL_RenderClear(SDL_Renderer*r){(void)r;return 0;}
void SDL_RenderPresent(SDL_Renderer*r){(void)r;}
static int jint(cJSON*j,const char*k){cJSON*v=cJSON_GetObjectItem(j,k);return v?v->valueint:0;}
static const char*jstr(cJSON*j,const char*k){cJSON*v=cJSON_GetObjectItem(j,k);return cJSON_IsString(v)?v->valuestring:NULL;}
static const char*ep_display_title(cJSON*j){return jstr(j,"title");}
static SDL_Texture*cover_get(const char*s){(void)s;return NULL;}
static void ui_cover(SDL_Texture*t,SDL_Rect*r){(void)t;(void)r;}
static void fill_rect(int a,int b,int c,int d,SDL_Color e){(void)a;(void)b;(void)c;(void)d;(void)e;}
static void ui_header(const char*a,const char*b,const char*c){(void)a;(void)b;(void)c;}
static void ui_panel(int a,int b,int c,int d,SDL_Color e){(void)a;(void)b;(void)c;(void)d;(void)e;}
static void text_draw(SDL_Renderer*r,const char*s,int a,int b,SDL_Color c,int d){(void)r;(void)s;(void)a;(void)b;(void)c;(void)d;}
static void text_clip(const char*s,int a,int b,SDL_Color c,int d,int e){(void)s;(void)a;(void)b;(void)c;(void)d;(void)e;}
static void text_center_at(const char*s,int a,int b,int c,SDL_Color d,int e){(void)s;(void)a;(void)b;(void)c;(void)d;(void)e;}
#include "episode_countdown_actual.inc"
int main(void){
 cJSON*ep=cJSON_Parse("{\"id\":42,\"title\":\"Next\",\"season\":3,\"episode\":2}");assert(ep);
 Uint32 starts[]={0,100000,UINT32_MAX-1200};
 for(unsigned i=0;i<sizeof(starts)/sizeof(*starts);i++){
  ticks=starts[i];mode=emitted=frames=0;g_running=1;
  assert(prompt_next_episode(ep,NULL)==1);
  assert(ticks-starts[i]>=5000&&ticks-starts[i]<5020&&!emitted);
 }
 for(int input=1;input<=5;input++){
  ticks=frames=emitted=0;mode=input;g_running=1;
  assert(prompt_next_episode(ep,NULL)==(input==3));assert(!frames);
 }
 cJSON_Delete(ep);
 puts("NATIVE AUTOPLAY COUNTDOWN OK: advances with ZERO input after5s, monotonic tick wrap, A immediate, B/Minus/touch/quit cancel; SDL rendering/time simulated.");
}
