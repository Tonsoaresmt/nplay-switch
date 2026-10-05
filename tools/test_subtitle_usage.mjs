// Real C control/store with simulated pipeline, decoder, font and network.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const read=p=>readFileSync(p,'utf8').replace(/\r\n/g,'\n');
const player=read('source/player.c'),ui=read('source/player_ui.c');
function section(s,a,b) {
  const start=s.indexOf(a),end=s.indexOf(b,start);
  if(start<0||end<start)throw Error(`Review audit boundary: ${a}`);
  return s.slice(start,end);
}
mkdirSync('build',{recursive:true});
const cc=process.env.HOST_CC||'gcc';
function run(name,source,extra=[]) {
  const path=`build/audit_${name}.c`,exe=resolve(`build/audit_${name}`+(process.platform==='win32'?'.exe':''));
  writeFileSync(path,source);
  const build=spawnSync(cc,['-std=c11','-Wall','-Wextra','-Werror','-Itools/host-stubs','-Iinclude','-Ibuild',path,...extra,'-lm','-o',exe],
    {encoding:'utf8',windowsHide:true,timeout:30000});
  if(build.status!==0)throw Error(build.stderr||build.error?.message);
  const result=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:15000});
  process.stdout.write(result.stdout||'');
  if(result.status!==0)throw Error(result.stderr||result.error?.message||name+' failed to reproduce');
}
run('storage',read('tools/test_subtitle_usage_storage.c'),['source/subtitle_store.c','source/subtitle_queue.c']);

const typeStart=player.indexOf('typedef struct {\n    int item_id;\n    SDL_atomic_t session_id;');
const typeEnd=player.indexOf('} PlaybackHeartbeat;',typeStart);
if(typeStart<0||typeEnd<typeStart)throw Error('Review heartbeat types');
const api=read('include/api.h');
writeFileSync('build/player_supervisor_types.inc',section(api,'typedef enum {','} PlaybackSource;')+'} PlaybackSource;\n'+
  read('include/player.h').replace(/^#pragma once.*$/m,'').replace(/^#include.*$/gm,'')+'\n'+
  player.slice(typeStart,typeEnd+'} PlaybackHeartbeat;'.length));
writeFileSync('build/player_supervisor_function.inc',player.slice(player.indexOf('int player_run(')));
let supervisor=read('tools/player_supervisor_host.c').split('int main(void) {')[0];
supervisor=supervisor.replace('static int count, index_step, cancel_renew;',
  'static int count, index_step, cancel_renew;\nstatic int observed_hint[8], observed_priority[8], picked=1;');
supervisor=supervisor.replace('Step *s = &steps[index_step++];',`Step *s = &steps[index_step];
    observed_hint[index_step] = req->subtitle_hint;
    observed_priority[index_step] = req->subtitle_hint_priority;
    printf("ATTEMPT %d hint=%d priority=%d\\n",index_step+1,req->subtitle_hint,req->subtitle_hint_priority);
    g_player_subtitle_index=index_step==0?picked+1:0;\n    if(index_step==0){SubtitleTrackKey keys[2]={{\"en\",\"EN\",0,0},{\"pt\",\"PT\",0,0}};subtitle_choice_capture(&g_player_subtitle_choice,keys,2,picked,1);}\n    index_step++;`);
run('continuity',supervisor+String.raw`
int main(void) {
  PlayerRequest r={0};PlayerResult out;r.start_sec=3000;r.title="Fixture";
  r.playback.delivery=DELIVERY_R2;strcpy(r.playback.container,"m3u8");
  strcpy(r.playback.play_url,"https://example.invalid/fixture");r.renew_cb=r.fallback_cb=renew;
  setup();count=2;
  steps[0]=(Step){3000,3010,PLAYER_RESTART_SEEK,1,0};steps[1]=(Step){3010,3011,0,1,1};
  assert(player_run(NULL,NULL,&r,&out)==0);
  assert(observed_hint[1]==2&&observed_priority[1]==1);
  puts("PASS: exact choice 2 persists after seek.");
  setup();count=3;
  steps[0]=(Step){3000,3001,PLAYER_RESTART_TRACK,1,0};steps[1]=(Step){3001,0,-5,0,0};steps[2]=(Step){3001,3002,0,1,1};
  assert(player_run(NULL,NULL,&r,&out)==0);
  assert(observed_hint[1]==2&&observed_priority[1]==1&&observed_hint[2]==2&&observed_priority[2]==1);
  puts("PASS: failed early opening preserves choice 2.");
  picked=-1;setup();count=3;
  steps[0]=(Step){3000,3001,PLAYER_RESTART_SEEK,1,0};steps[1]=(Step){3001,0,-5,0,0};steps[2]=(Step){3001,3002,0,1,1};
  assert(player_run(NULL,NULL,&r,&out)==0);
  assert(observed_hint[2]==0&&observed_priority[2]==1);
  puts("PASS: deliberate OFF survives failed opening.");
}`,['source/player_sync.c','source/subtitle_choice.c']);

const selection=section(player,'    int restored_sub =','    int aidx = naud ?');
run('selection',String.raw`
#include "audio_policy.h"
#include "subtitle_choice.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
typedef struct {int subtitle_hint_priority,subtitle_hint,audio_pref,source_id;const SubtitleChoice *subtitle_choice;} Request;
typedef struct {const char *language,*name;} Track;
static const char *lang_norm(const char *s){return audio_language_normalize(s,NULL);}
static const char *stream_norm(const char **fmt,int i){return audio_language_normalize(fmt[i],NULL);}
static int choose(Request *req,const char *saved,const char *first,const char *second){
  int scur=-1,sub_chosen=0,nsub=2,naud=1,pt_audio=-1,known_audio=1,acur=0;
  int aidxs[]={2},sidxs[]={0,1},manifest_subtitle_fallback=1;
  const char *fmt[]={first,second,"ja"};Track manifest_subtitles[]={{first,"Track 1"},{second,"Track 2"}};
  SubtitleTrackKey subtitle_keys[2]={{{0},{0},0,0},{{0},{0},0,0}};
  for(int i=0;i<2;i++){snprintf(subtitle_keys[i].language,8,"%s",fmt[i]);snprintf(subtitle_keys[i].name,96,"%s",fmt[i]);}
  char pref_sub[32];snprintf(pref_sub,sizeof(pref_sub),"%s",saved);
`+selection+String.raw`
  return scur;
}
int main(void){
  SubtitleTrackKey keys[2]={{"pt","pt",0,0},{"pt","pt",0,0}}; SubtitleChoice choice={0};
  subtitle_choice_capture(&choice,keys,2,1,2); Request r={1,2,1,2,&choice};
  assert(choose(&r,"pt","pt","pt")==1);
  SubtitleTrackKey other[2]={{"en","en",0,0},{"pt","pt",0,0}};
  subtitle_choice_capture(&choice,other,2,1,2); r.source_id=3;
  assert(choose(&r,"pt","pt","en")==0);
  subtitle_choice_capture(&choice,other,2,-1,2); assert(choose(&r,"pt","pt","en")==-1);
  r.subtitle_choice=NULL;r.subtitle_hint_priority=0;assert(choose(&r,"off","pt","en")==-1);
  keys[0].rendition=101;keys[1].rendition=202;
  subtitle_choice_capture(&choice,keys,2,1,2);
  SubtitleTrackKey swapped[2]={keys[1],keys[0]};
  assert(subtitle_choice_resolve(&choice,swapped,2,2)==0);
  assert(subtitle_choice_resolve(&choice,swapped,2,3)==0);
  swapped[0].rendition=303;swapped[1].rendition=404;
  assert(subtitle_choice_resolve(&choice,swapped,2,3)==-2);
  assert(subtitle_choice_resolve(&choice,NULL,0,3)==-2);
  keys[0].forced=1;keys[0].rendition=0;keys[1].rendition=0;
  subtitle_choice_capture(&choice,keys,2,0,2);swapped[0]=keys[1];swapped[1]=keys[0];
  assert(subtitle_choice_resolve(&choice,swapped,2,3)==1);
  assert(subtitle_rendition_key("https://example.invalid/pt.vtt?token=old")==
         subtitle_rendition_key("https://example.invalid/pt.vtt?token=new#ignored"));
  assert(subtitle_rendition_key("https://example.invalid/pt.vtt")!=
         subtitle_rendition_key("https://example.invalid/en.vtt"));
  assert(subtitle_rendition_key("https://example.invalid/sub.vtt?language=pt&token=old")!=
         subtitle_rendition_key("https://example.invalid/sub.vtt?language=en&token=new"));
  assert(subtitle_rendition_key("https://example.invalid/sub.vtt?format=full&token=old")==
         subtitle_rendition_key("https://example.invalid/sub.vtt?format=full&token=new"));
  puts("PASS: real selector keeps exact PT, remaps order and respects deliberate OFF.");
}`,['source/audio_policy.c','source/subtitle_choice.c']);

const utf8=section(read('tools/test_subtitle_usage_storage.c'),'static int valid_utf8','int main(void)');
run('selection-publication',String.raw`
#include "subtitle_choice.h"
#include <assert.h>
#include <stdio.h>
static SubtitleChoice g_player_subtitle_choice;
static void publish(int scur,int sub_chosen,int nsub){
  struct {int source_id;} r={2},*req=&r;
  SubtitleTrackKey subtitle_keys[2]={{"pt","PT",0,0},{"en","EN",0,0}};
`+section(player,'    // Desired selection survives','    g_player_audio_index = naud ?')+String.raw`
}
int main(void){
  publish(-1,0,2);assert(!g_player_subtitle_choice.valid);
  publish(0,0,2);assert(g_player_subtitle_choice.valid&&g_player_subtitle_choice.index==0);
  g_player_subtitle_choice.valid=0;publish(-1,1,0);
  assert(g_player_subtitle_choice.valid&&g_player_subtitle_choice.index==-1);
  puts("PASS: automatic absence is not deliberate OFF; selected track and explicit OFF are published.");
}`,['source/subtitle_choice.c']);
run('metadata',String.raw`
#include <SDL.h>
#include "hls_manifest.h"
#include "audio_policy.h"
#include "subtitle_choice.h"
#include "subtitle_store.h"
#include "subtitle_utf8.h"
#include "vtt_stream.h"
#include <assert.h>
#include <stdio.h>
typedef int SDL_mutex;
static int locks;
static void SDL_LockMutex(SDL_mutex *m){(void)m;locks++;}
static void SDL_UnlockMutex(SDL_mutex *m){(void)m;locks--;}
`+section(player,'typedef struct ProgressiveSubtitle','static void progressive_subtitle_stop(ProgressiveSubtitle *stream);')+
section(player,'static int external_subtitle_count(','static const char *external_subtitle_text(')+
section(player,'static void external_subtitle_keys(','static void external_subtitle_labels(')+String.raw`
int main(void){
  HlsManifestTrack tracks[17]={0};SubtitleTrackKey keys[17]={0};
  for(int i=0;i<17;i++){
    snprintf(tracks[i].language,sizeof(tracks[i].language),"pt-BR");
    snprintf(tracks[i].name,sizeof(tracks[i].name),"PT %d",i);
    snprintf(tracks[i].uri,sizeof(tracks[i].uri),"https://example.invalid/%d.vtt?token=fixture",i);
  }
  tracks[1].forced=1;keys[16].forced=99;
  external_subtitle_keys(keys,tracks,17);
  assert(!strcmp(keys[0].language,"pt")&&keys[1].forced==1&&keys[16].forced==99);
  assert(keys[0].rendition&&keys[0].rendition!=keys[1].rendition);
  ProgressiveSubtitle p={0};ExternalSubtitleStore wrapper={0};wrapper.progressive=&p;
  assert(subtitle_store_add(&p.store.cues,10,12,"real progressive speech"));
  assert(subtitle_store_count(&wrapper.cues)==0);
  assert(external_subtitle_count(&wrapper)==1&&locks==0);
  subtitle_store_free(&p.store.cues);
  puts("PASS: late external metadata, 16-track cap, progressive count under mutex.");
}`,['source/audio_policy.c','source/subtitle_choice.c','source/subtitle_store.c','source/subtitle_queue.c']);
run('text','#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n#include "subtitle_utf8.h"\n'+utf8+
  'static int text_measure(const char *s,int style,int *w,int *h){(void)style;*w=(int)strlen(s)*11;*h=20;return 0;}\n'+
  section(ui,'// Quebra por palavra usando a medida real','void pui_format_time(')+
  section(player,'static void ass_to_text(','typedef struct ProgressiveSubtitle')+String.raw`
int main(void){
  WrapCache cache={0};char word[230]="A";
  for(int i=0;i<70;i++)memcpy(word+1+i*3,"\xe3\x81\x82",3);
  word[211]=0;
  int lines=wrap(&cache,word,0,1080,4),invalid=0;
  for(int i=0;i<lines;i++)if(!valid_utf8((unsigned char*)cache.lines[i]))invalid++;
  printf("WRAP UTF8: invalid lines=%d.\n",invalid);assert(invalid==0);
  char text[410],out[400];memset(text,'a',398);memcpy(text+398,"\xc3\xa9",3);
  ass_to_text(text,out,sizeof(out));assert(valid_utf8((unsigned char*)out));
  puts("PASS: ASS converter preserves UTF8.");
}`);

writeFileSync('build/audit_subtitle_completion.inc',section(player,'typedef struct { const unsigned char *data;','typedef struct {\n    const char *url, *sid;'));
writeFileSync('build/audit_progressive_functions.inc',section(player,'static int progressive_subtitle_block(','static void progressive_subtitle_stop(ProgressiveSubtitle *stream) {'));
let progressive=read('tools/subtitle_completion_host.c').split('int main(void) {')[0];
progressive=progressive.replace('#include "subtitle_limits.h"','#include "subtitle_limits.h"\n#include "vtt_stream.h"')
  .replace('static int reads,terminal,','static const char *decoded_text="new dialogue";\nstatic int reads,terminal,')
  .replace('static AVSubtitleRect r={SUBTITLE_TEXT,"new dialogue",NULL};','static AVSubtitleRect r={SUBTITLE_TEXT,"new dialogue",NULL};r.text=(char*)decoded_text;')
  .replace('#include "subtitle_completion.inc"','#include "audit_subtitle_completion.inc"');
run('progressive',progressive+String.raw`
typedef struct {ExternalSubtitleStore store;void *mutex;SDL_atomic_t cancel,ready,done;VttStream parser;char url[2048];int result,reported;} ProgressiveSubtitle;
static void SDL_LockMutex(void *m){(void)m;}
static void SDL_UnlockMutex(void *m){(void)m;}
int SDL_AtomicSet(SDL_atomic_t *a,int v){int old=a->value;a->value=v;return old;}
Uint32 SDL_GetTicks(void){return 0;}
void SDL_Delay(Uint32 ms){(void)ms;}
static int attempts,stream_mode;
long net_stream_text(const char *url,size_t limit,SDL_atomic_t *cancel,net_text_chunk_cb callback,void *data){
  (void)url;(void)cancel;assert(limit==SUBTITLE_DOWNLOAD_MAX);attempts++;
  if(stream_mode==0)return 503;
  const char *one="WEBVTT\n\n00:00:10.000 --> 00:00:12.000\nFala\n\n";
  reads=0;terminal=AVERROR_EOF;decoded_text="new dialogue";
  return callback(one,strlen(one),data)?200:-23;
}
#include "audit_progressive_functions.inc"
int main(void){
  (void)check;(void)player_hls_io_open;(void)player_hls_io_close;
  AVCodecParameters par={AV_CODEC_ID_NONE};stream.codecpar=&par;http_code=200;
  ProgressiveSubtitle s={0};const char *cue="00:00:10.000 --> 00:00:12.000\nFala\n";
  const char *note="NOTE comment with --> not a cue";
  assert(progressive_subtitle_block(&s,note,strlen(note))==1);
  const char *empty="00:00:10.000 --> 00:00:12.000";
  assert(progressive_subtitle_block(&s,empty,strlen(empty))==1);
  reads=0;terminal=AVERROR_EOF;assert(progressive_subtitle_block(&s,cue,strlen(cue))==1);
  reads=0;decoded_text="m 0 0 l 100 100";
  const char *graphic="00:00:12.000 --> 00:00:14.000\nm 0 0 l 100 100\n";
  int ignored=progressive_subtitle_block(&s,graphic,strlen(graphic));assert(ignored==1);
  printf("PASS: ignored drawing continues stream, rc=%d.\n",ignored);
  reads=0;decoded_text="dialogue AFTER graphic";
  assert(progressive_subtitle_block(&s,cue,strlen(cue))==1);
  assert(strstr(subtitle_store_text(&s.store.cues,10.5),"AFTER graphic"));
  reads=2;assert(progressive_subtitle_block(&s,cue,strlen(cue))==0); // zero decoded packets
  reads=0;terminal=AVERROR(EIO);assert(progressive_subtitle_block(&s,graphic,strlen(graphic))==0);
  reads=0;terminal=AVERROR_EOF;decoded_text="";
  assert(progressive_subtitle_block(&s,cue,strlen(cue))==1); // valid ignored empty output
  assert(load_external_subtitle_data("https://example.invalid/sub.vtt",&s.store,1,&s.cancel,NULL,0)==-1);
  s.cancel.value=1;assert(progressive_subtitle_block(&s,cue,strlen(cue))==0);s.cancel.value=0;
  vtt_stream_init(&s.parser,progressive_subtitle_block,&s);attempts=0;stream_mode=0;
  progressive_subtitle_worker(&s);assert(attempts==4&&s.done.value==1&&s.result==-1);
  assert(progressive_subtitle_failed_once(&s)==1);
  assert(progressive_subtitle_failed_once(&s)==0);
  printf("EXHAUSTED: attempts=%d result=%d partial cues remain=%d.\n",attempts,s.result,external_subtitle_count(&s.store));
  vtt_stream_free(&s.parser);external_subtitle_clear(&s.store);memset(&s,0,sizeof(s));
  vtt_stream_init(&s.parser,progressive_subtitle_block,&s);attempts=0;stream_mode=1;
  progressive_subtitle_worker(&s);assert(attempts==1&&s.result==0&&external_subtitle_count(&s.store)==2);
  assert(progressive_subtitle_failed_once(&s)==0);
  s.reported=0;s.done.value=0;s.result=-1;assert(progressive_subtitle_failed_once(&s)==0);
  s.done.value=1;s.cancel.value=1;assert(progressive_subtitle_failed_once(&s)==0);
  puts("CONTRACT LIMIT: early clean HTTP200 is accepted; no completeness marker (not proof the real source is truncated).");
  vtt_stream_free(&s.parser);external_subtitle_clear(&s.store);assert(!io_active&&!formats&&!codecs&&!packets);
}`,['source/subtitle_queue.c','source/subtitle_store.c','source/vtt_stream.c']);
console.log('SUBTITLE USAGE REGRESSIONS OK: simulated FFmpeg/network/font, not Switch hardware.');
