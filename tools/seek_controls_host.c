#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "player_seek_input.h"
#include "settings_layout.h"
typedef uint32_t Uint32;
#define JOY_A 0
#define JOY_B 1
#define JOY_L 6
#define JOY_R 7
#define JOY_ZL 8
#define JOY_ZR 9
#define SDL_TICKS_PASSED(a,b) ((int32_t)((a)-(b))>=0)
static unsigned held;
static Uint32 ticks;
static int queued,paused,audio_paused;
static int running=1,have_video_frame=1,seekable_stream=1;
static int track_menu,episodes_menu,next_selected,timeline_seek,quick_seek_active;
static int timeline_seek_was_paused,seek_axis_lock;
static double cur_pos=100,quick_seek_target=160,timeline_seek_from,timeline_seek_target;
static Uint32 quick_seek_deadline,timeline_seek_tick;
static char notice[160];
static Uint32 notice_until;
static double dur=7200;
static PlayerSeekHold seek_hold;
static int *joy=&running;
static int SDL_PollEvent(void *e){(void)e;int result=queued;queued=0;return result;}
static int SDL_JoystickGetButton(void*j,int b){(void)j;return (held>>(b-6))&1;}
static Uint32 SDL_GetTicks(void){return ticks;}
static void SDL_PauseAudioDevice(int a,int b){(void)a;audio_paused=b;}
static void diag_player_event(const char*a,const char*b,const char*c,...){(void)a;(void)b;(void)c;}
static void pui_format_time(double t,char*out,size_t cap){snprintf(out,cap,"%.0f",t);}
static void press_step(int b){
#include "seek_step.inc"
}
static void cancel_or_other(int b){int local_seek_button=-1;for(int once=0;once<1;once++){
#include "seek_cancel.inc"
}}
static int poll_action(void){int adev=1;int e=0;for(;;){
#include "seek_poll.inc"
 return local_seek_button;
 }return -1;}
#define DIR_APP "."
static int g_media_profile_id;
#include "seek_store.inc"
static int g_prefs_sel,g_pref_seek_stick;
static void toast(const char*s){assert(s);}
#include "seek_pref.inc"
    (void)direction; assert(!"Unexpected remote preference in local test");
}
static void reset_poll(void){
 seek_hold=(PlayerSeekHold){0};held=ticks=queued=0;running=have_video_frame=seekable_stream=1;
 track_menu=episodes_menu=next_selected=timeline_seek=quick_seek_active=paused=audio_paused=0;
 quick_seek_deadline=0;
}
static uint32_t random_state=0x4e504c59;
static uint32_t next_random(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void scenario_matrix(void){
 const double lengths[]={0,0.5,1,60,1440,7200};
 unsigned actions=0,commits=0,cancels=0,modals=0;
 for(int test=0;test<20000;test++){
  reset_poll();dur=lengths[next_random()%6];cur_pos=dur>0?dur*(next_random()%100)/100.0:3000;
  paused=next_random()%2;ticks=next_random();double expected=cur_pos;
  for(int step=0;step<10;step++){
   int b=6+(int)(next_random()%4);double delta=b==6?-10:b==7?10:b==8?-60:60;
   expected+=delta;if(expected<0)expected=0;
   if(dur>0&&expected>(dur>1?dur-1:0))expected=dur>1?dur-1:0;
   press_step(b);assert(quick_seek_active&&!timeline_seek&&quick_seek_target==expected);
   ticks+=next_random()%90;actions++;
  }
  int outcome=test%4;
  if(outcome==0){cancel_or_other(JOY_B);ticks+=1000;assert(poll_action()==-1&&!timeline_seek);cancels++;}
  else if(outcome==1){episodes_menu=1;ticks+=1000;assert(poll_action()==-1&&!quick_seek_active&&!timeline_seek);episodes_menu=0;assert(poll_action()==-1);modals++;}
  else if(outcome==2){seekable_stream=0;ticks+=1000;assert(poll_action()==-1&&!quick_seek_active&&!timeline_seek);modals++;}
  else {int was_paused=paused;ticks+=701;assert(poll_action()==JOY_A&&timeline_seek_target==expected&&timeline_seek_was_paused==was_paused);commits++;}
 }
 printf("PASS deterministic controls matrix: %u real C steps, %u commits, %u cancels, %u modal/unsupported-source cases; short/unknown/2h durations, pause and wrapping clocks.\n",actions,commits,cancels,modals);
}
int main(void){
 PlayerSeekHold h={0};
 assert(player_seek_hold_tick(&h,2,0,1)==-1);
 assert(player_seek_hold_tick(&h,2,449,1)==-1);
 assert(player_seek_hold_tick(&h,2,450,1)==7);
 assert(player_seek_hold_tick(&h,2,599,1)==-1);
 assert(player_seek_hold_tick(&h,2,600,1)==7);
 assert(player_seek_hold_tick(&h,2,10000,1)==7);
 assert(player_seek_hold_tick(&h,2,10000,1)==-1);
 for(int i=0;i<4;i++){h=(PlayerSeekHold){0};assert(player_seek_hold_tick(&h,1u<<i,UINT32_MAX-100,1)==-1);assert(player_seek_hold_tick(&h,1u<<i,349,1)==i+6);}
 h=(PlayerSeekHold){0};assert(player_seek_hold_tick(&h,3,0,1)==-1);assert(player_seek_hold_tick(&h,3,1000,1)==-1);
 assert(player_seek_hold_tick(&h,2,1100,0)==-1);assert(player_seek_hold_tick(&h,2,5000,1)==-1);
 assert(player_seek_hold_tick(&h,0,5001,1)==-1);assert(player_seek_hold_tick(&h,2,5002,1)==-1);assert(player_seek_hold_tick(&h,2,5452,1)==7);
 assert(player_seek_target(10,100,-60)==0);assert(player_seek_target(90,100,60)==99);
 assert(player_seek_target(100,0,60)==160);assert(player_seek_target(NAN,100,10)==10);
 assert(player_seek_target(0,0.5,60)==0&&player_seek_target(0,1,60)==0);
 double middle=0;
 for(int i=0;i<60;i++) middle=player_seek_target(middle,7200,60);
 assert(middle==3600);for(int i=0;i<30;i++)middle=player_seek_target(middle,7200,-60);assert(middle==1800);
 puts("PASS held shoulders: four buttons, bounded 450/150ms repeats, no catch-up, wrap, conflicts, release after modal; targets bounded/unknown duration.");
 for(int x=-32768;x<=32767;x++){int lock=1,dir=0;Uint32 since=0;assert(!player_seek_stick_arm(0,1,x,0,&lock,&dir,&since));assert(!player_seek_stick_arm(0,1,x,1000,&lock,&dir,&since));}
 int lock=1,dir=0;Uint32 since=0;
 assert(!player_seek_stick_arm(1,1,32767,0,&lock,&dir,&since));
 assert(!player_seek_stick_arm(1,1,0,1,&lock,&dir,&since));assert(!lock);
 assert(!player_seek_stick_arm(1,1,32767,10,&lock,&dir,&since));
 assert(!player_seek_stick_arm(1,1,32767,559,&lock,&dir,&since));assert(player_seek_stick_arm(1,1,32767,560,&lock,&dir,&since));
 assert(!player_seek_stick_arm(1,1,-32768,561,&lock,&dir,&since)); // Direction change re-arms.
 assert(!player_seek_stick_arm(1,1,-32768,1110,&lock,&dir,&since));
 assert(player_seek_stick_arm(1,1,-32768,1111,&lock,&dir,&since));
 assert(!player_seek_stick_arm(1,0,32767,561,&lock,&dir,&since));assert(lock);
 assert(!player_seek_stick_arm(1,1,-32768,2000,&lock,&dir,&since));
 player_seek_stick_arm(1,1,0,2001,&lock,&dir,&since);
 player_seek_stick_arm(1,1,-32768,UINT32_MAX-100,&lock,&dir,&since);
 assert(player_seek_stick_arm(1,1,-32768,449,&lock,&dir,&since));
 puts("PASS right stick: OFF ignores full range; neutral gate, dead zone, hold, modal exclusion and clock wrap.");
 reset_poll();quick_seek_active=1;ticks=1000;quick_seek_deadline=700;queued=1;
 assert(poll_action()==-1 && quick_seek_active && !timeline_seek); // Real event always first.
 assert(poll_action()==JOY_A && !quick_seek_active && timeline_seek && audio_paused);
 assert(timeline_seek_target==160 && timeline_seek_from==100);
 for(int modal=0;modal<3;modal++){reset_poll();quick_seek_active=1;ticks=1000;if(modal==0)track_menu=1;else if(modal==1)episodes_menu=1;else next_selected=1;
  assert(poll_action()==-1 && !quick_seek_active && !timeline_seek);}
 reset_poll();quick_seek_active=1;ticks=1000;held=2;assert(poll_action()==-1 && !timeline_seek);
 ticks=1450;assert(poll_action()==7 && !timeline_seek);held=0;paused=1;
 assert(poll_action()==JOY_A && timeline_seek_was_paused);
 reset_poll();cur_pos=100;press_step(JOY_R);press_step(JOY_ZR);press_step(JOY_L);
 assert(quick_seek_target==160 && !timeline_seek);cancel_or_other(JOY_B);
 assert(!quick_seek_active && seek_hold.blocked && !timeline_seek);
 reset_poll();cur_pos=0;held=8;press_step(JOY_ZR);poll_action();
 for(int i=1;i<60;i++){ticks+=450;int b=poll_action();assert(b==JOY_ZR);press_step(b);assert(!timeline_seek);}
 assert(quick_seek_target==3600);held=0;ticks+=701;
 assert(poll_action()==JOY_A && timeline_seek_target==3600);
 reset_poll();press_step(JOY_R);cancel_or_other(3);assert(!quick_seek_active); // Open audio/menu.
 cur_pos=100;dur=0;press_step(JOY_R);assert(quick_seek_target==110);cancel_or_other(JOY_B);
 dur=7200;
 puts("PASS actual event dispatcher: input before commit, no global synthetic A, no modal/episode confirmation, held seek not applied until release, pause intent retained.");
 remove("seek_stick_101.txt");remove("seek_stick_202.txt");
 g_media_profile_id=0;assert(!store_load_seek_stick()&&!store_save_seek_stick(1));
 g_media_profile_id=101;assert(!store_load_seek_stick());g_prefs_sel=4;save_selected_preference(1);assert(g_pref_seek_stick&&store_load_seek_stick());
 g_media_profile_id=202;assert(!store_load_seek_stick());assert(store_save_seek_stick(0));assert(!store_load_seek_stick());
 g_media_profile_id=101;assert(store_load_seek_stick());assert(store_save_seek_stick(0));assert(!store_load_seek_stick());
 FILE*f=fopen("seek_stick_101.txt","wb");assert(f);fputs("2junk",f);fclose(f);assert(!store_load_seek_stick());
 remove("seek_stick_101.txt");remove("seek_stick_202.txt");
 for(int i=0;i<5;i++){int y=settings_row_y(1,i);assert(settings_row_at(1,5,500,y)==i);assert(settings_row_at(1,5,500,y+52)==-1);assert(y+52<=620);}
 puts("PASS actual persistence/settings: default OFF, local branch without HTTP, profile isolation, reload, OFF, malformed data, atomic save and five touch rows fit.");
 scenario_matrix();
 return 0;
}
