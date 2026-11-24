#include "player_clock.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t Uint32;
#define SDL_TICKS_PASSED(a,b) ((int32_t)((b)-(a))<=0)
#define SDL_PEEKEVENT 1
#define SDL_JOYBUTTONDOWN 2
static Uint32 ticks;
static Uint32 SDL_GetTicks(void){return ticks;}
static void SDL_Delay(Uint32 n){ticks+=n;}
static void SDL_PumpEvents(void){}
static int player_lifecycle_pending(void){return 0;}
static int SDL_PeepEvents(void*p,int n,int a,int lo,int hi){(void)p;(void)n;(void)a;(void)lo;(void)hi;return 0;}
#include "audit062_vfr_pts.inc"
int main(void){
 double wall=0,first=actual_vfr_pts[0],next=actual_vfr_pts[1];
 assert(player_clock_master(0,first,0,0,0,&wall,1)==first);
 double master=player_clock_master(0,next,1,1,1,&wall,0),delay=next-master;
 Uint32 sync_since_present_ms=0;
#include "audit062_wait.inc"
 assert(delay==0.35&&ticks==350&&next-ticks/1000.0>3.6&&sync_since_present_ms==350);
 printf("REPRODUCED real VFR timestamps: framePTS%.3f presented at modeled%.3fs, %.3fs early due to 350ms cap. Native wait/clock C, render/device simulated.\n",next,ticks/1000.0,next-ticks/1000.0);
 return 0;
}
