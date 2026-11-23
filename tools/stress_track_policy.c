// Healthy invariants complement, but cannot cancel out, the negative findings.
#include "audio_policy.h"
#include "subtitle_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static AudioTrackInfo pool[]={
 {"pt-BR","Portuguese",1,0,0},{"por","Dublado",0,0,0},{"pob","Brasil",0,0,0},
 {"eng","English",1,0,0},{"jpn","Japanese",0,0,0},{"und","Portugues Brasil",0,0,0},
 {"pt","Commentary",1,1,0},{"eng","Director commentary",0,1,0}
};
static unsigned layouts,calls;
static void visit(AudioTrackInfo*tracks,int count,unsigned used){
 if(count){
  layouts++;int has_pt=0;
  for(int i=0;i<count;i++)if(!tracks[i].is_commentary&&!strcmp(audio_language_normalize(tracks[i].language,tracks[i].title),"pt"))has_pt=1;
  int selected=audio_policy_choose(tracks,count,0,"en","ja",0,0,0);calls++;
  assert(selected>=0&&selected<count);
  if(has_pt)assert(!tracks[selected].is_commentary&&!strcmp(audio_language_normalize(tracks[selected].language,tracks[selected].title),"pt"));
  for(int i=0;i<count;i++){
   selected=audio_policy_choose(tracks,count,0,NULL,"pt",i+1,2,0);calls++;
   assert(selected==i); // explicit manual exact-source choice must survive
  }
 }
 if(count==5)return;
 for(unsigned i=0;i<sizeof(pool)/sizeof(*pool);i++)if(!(used&(1u<<i))){tracks[count]=pool[i];visit(tracks,count+1,used|(1u<<i));}
}
int main(void){
 AudioTrackInfo tracks[16]={0};visit(tracks,0,0);
 assert(layouts==8800&&calls>40000);
 assert(audio_policy_choose(NULL,0,0,NULL,NULL,0,0,0)==-1);
 SubtitleStore store={0};char text[32];
 for(int i=0;i<SUBTITLE_STORE_MAX_CUES;i++){
  snprintf(text,sizeof(text),"cue %d",i);assert(subtitle_store_add(&store,i,i+.5,text));
 }
 assert(subtitle_store_count(&store)==SUBTITLE_STORE_MAX_CUES&&store.text_len<=SUBTITLE_STORE_MAX_TEXT);
 assert(strstr(subtitle_store_text(&store,SUBTITLE_STORE_MAX_CUES-1+.1),"cue 199999"));
 assert(!subtitle_store_add(&store,SUBTITLE_STORE_MAX_CUES,SUBTITLE_STORE_MAX_CUES+.5,"over limit"));
 assert(store.truncated==1&&subtitle_store_count(&store)==SUBTITLE_STORE_MAX_CUES);
 assert(strstr(subtitle_store_text(&store,10.1),"cue 10"));
 size_t text_bytes=store.text_len;subtitle_store_free(&store);
 printf("HEALTHY INVARIANTS: %u track layouts / %u policy calls preserve ordinary PT and exact manual choice; 200000 cues remain readable through the tail and over-cap insertion fails safely (%zu text bytes). Host only, not GPU/decoder/SD.\n",layouts,calls,text_bytes);
 return 0;
}
