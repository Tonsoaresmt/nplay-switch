// Diagnostic reproduction of known subtitle defects. Exit 0 means the audit
// ran, NOT that the player is healthy. Do not add this to validate_release as
// a passing regression test; change assertions when implementing the fixes.
// App source, server state, NRO and release are never modified by this tool.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const baseline=process.argv.includes('--baseline');
if(!baseline)throw Error('Historical audit: use --baseline (c8ef79f), or test_subtitle_usage.mjs for corrected code.');
const read=p=>{
  if(p.startsWith('source/')){
    const git=process.platform==='win32'?'C:/Program Files/Git/cmd/git.exe':'git';
    const r=spawnSync(git,['show','c8ef79f:'+p],{encoding:'utf8',windowsHide:true,timeout:30000});
    if(r.status!==0)throw Error(r.stderr);
    return r.stdout.replace(/\r\n/g,'\n');
  }
  return readFileSync(p,'utf8').replace(/\r\n/g,'\n');
};
const player=read('source/player.c'),ui=read('source/player_ui.c');
function section(s,a,b) {
  const start=s.indexOf(a),end=s.indexOf(b,start);
  if(start<0||end<start)throw Error(`Review audit boundary: ${a}`);
  return s.slice(start,end);
}
mkdirSync('build',{recursive:true});
const cc=process.env.HOST_CC||'gcc';
function run(name,source,extra=[]) {
  extra=extra.map(p=>{
    if(!p.startsWith('source/'))return p;
    const target='build/audit_baseline_'+p.slice(7);writeFileSync(target,read(p));return target;
  });
  const path=`build/audit_${name}.c`,exe=resolve(`build/audit_${name}`+(process.platform==='win32'?'.exe':''));
  writeFileSync(path,source);
  const build=spawnSync(cc,['-std=c11','-Wall','-Wextra','-Werror','-Itools/host-stubs','-Iinclude','-Ibuild',path,...extra,'-lm','-o',exe],
    {encoding:'utf8',windowsHide:true,timeout:30000});
  if(build.status!==0)throw Error(build.stderr||build.error?.message);
  const result=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:15000});
  process.stdout.write(result.stdout||'');
  if(result.status!==0)throw Error(result.stderr||result.error?.message||name+' failed to reproduce');
}
run('storage',read('tools/audit_subtitle_storage.c'),['source/subtitle_store.c','source/subtitle_queue.c']);

const typeStart=player.indexOf('typedef struct {\n    int item_id;\n    SDL_atomic_t session_id;');
const typeEnd=player.indexOf('} PlaybackHeartbeat;',typeStart);
if(typeStart<0||typeEnd<typeStart)throw Error('Review heartbeat types');
const api=read('include/api.h');
writeFileSync('build/player_supervisor_types.inc',section(api,'typedef enum {','} PlaybackSource;')+'} PlaybackSource;\n'+
  read('include/player.h').replace(/^#pragma once.*$/m,'').replace(/^#include.*$/gm,'')+'\n'+
  player.slice(typeStart,typeEnd+'} PlaybackHeartbeat;'.length));
writeFileSync('build/player_supervisor_function.inc',player.slice(player.indexOf('int player_run(')));
let supervisor=read('tools/player_supervisor_host.c').split('int main(void) {')[0];
supervisor=supervisor.replace('static SubtitleChoice g_player_subtitle_choice;','');
supervisor=supervisor.replace('static int count, index_step, cancel_renew;',
  'static int count, index_step, cancel_renew;\nstatic int observed_hint[8], observed_priority[8];');
supervisor=supervisor.replace('Step *s = &steps[index_step++];',`Step *s = &steps[index_step];
    observed_hint[index_step] = req->subtitle_hint;
    observed_priority[index_step] = req->subtitle_hint_priority;
    printf("ATTEMPT %d hint=%d priority=%d\\n",index_step+1,req->subtitle_hint,req->subtitle_hint_priority);
    g_player_subtitle_index=index_step==0?2:0;index_step++;`);
run('continuity',supervisor+String.raw`
int main(void) {
  PlayerRequest r={0};PlayerResult out;r.start_sec=3000;r.title="Fixture";
  r.playback.delivery=DELIVERY_R2;strcpy(r.playback.container,"m3u8");
  strcpy(r.playback.play_url,"https://example.invalid/fixture");r.renew_cb=r.fallback_cb=renew;
  setup();count=2;
  steps[0]=(Step){3000,3010,PLAYER_RESTART_SEEK,1,0};steps[1]=(Step){3010,3011,0,1,1};
  assert(player_run(NULL,NULL,&r,&out)==0);
  assert(observed_hint[1]==2&&observed_priority[1]==0);
  puts("SEEK DEFECT: exact choice 2 is only a non-priority hint.");
  setup();count=3;
  steps[0]=(Step){3000,3001,PLAYER_RESTART_TRACK,1,0};steps[1]=(Step){3001,0,-5,0,0};steps[2]=(Step){3001,3002,0,1,1};
  assert(player_run(NULL,NULL,&r,&out)==0);
  assert(observed_hint[1]==2&&observed_priority[1]==1&&observed_hint[2]==0&&observed_priority[2]==1);
  puts("REOPEN DEFECT: early failed opening changes previous track 2 into forced OFF.");
}`,['source/player_sync.c']);

const selection=section(player,'    if (req->subtitle_hint_priority) {','    int aidx = naud ?');
run('selection',String.raw`
#include "audio_policy.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
typedef struct {int subtitle_hint_priority,subtitle_hint,audio_pref;} Request;
typedef struct {const char *language,*name;} Track;
static const char *lang_norm(const char *s){return audio_language_normalize(s,NULL);}
static const char *stream_norm(const char **fmt,int i){return audio_language_normalize(fmt[i],NULL);}
static int choose(Request *req,const char *saved,const char *first,const char *second){
  int scur=-1,sub_chosen=0,nsub=2,naud=1,pt_audio=-1,known_audio=1,acur=0;
  int aidxs[]={2},sidxs[]={0,1},manifest_subtitle_fallback=1;
  const char *fmt[]={first,second,"ja"};Track manifest_subtitles[]={{first,"Track 1"},{second,"Track 2"}};
  char pref_sub[32];snprintf(pref_sub,sizeof(pref_sub),"%s",saved);
`+selection+String.raw`
  return scur;
}
int main(void){
  Request r={0,2,1};int s=choose(&r,"pt","pt","pt");assert(s==0);
  printf("SELECTION DEFECT: Portuguese track 2 after seek becomes track %d.\n",s+1);
  r.subtitle_hint_priority=1;s=choose(&r,"pt","pt","en");assert(s==1);
  puts("REORDER DEFECT: source with reordered tracks restores English by old index.");
  r.subtitle_hint=0;assert(choose(&r,"pt","pt","en")==-1);
  r.subtitle_hint_priority=0;assert(choose(&r,"off","pt","en")==-1);
  puts("POLICY NOTE, not defect: remembered OFF wins automatic subtitles in Legendado mode.");
}`,['source/audio_policy.c']);

const utf8=section(read('tools/audit_subtitle_storage.c'),'static int valid_utf8','int main(void)');
run('text','#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n'+utf8+
  'static int text_measure(const char *s,int style,int *w,int *h){(void)style;*w=(int)strlen(s)*11;*h=20;return 0;}\n'+
  section(ui,'// Quebra por palavra usando a medida real','void pui_format_time(')+
  section(player,'static void ass_to_text(','typedef struct ProgressiveSubtitle')+String.raw`
int main(void){
  WrapCache cache={0};char word[230]="A";
  for(int i=0;i<70;i++)memcpy(word+1+i*3,"\xe3\x81\x82",3);
  word[211]=0;
  int lines=wrap(&cache,word,0,1080,4),invalid=0;
  for(int i=0;i<lines;i++)if(!valid_utf8((unsigned char*)cache.lines[i]))invalid++;
  printf("WRAP UTF8 DEFECT: invalid lines=%d.\n",invalid);assert(invalid>0);
  char text[410],out[400];memset(text,'a',398);memcpy(text+398,"\xc3\xa9",3);
  ass_to_text(text,out,sizeof(out));assert(!valid_utf8((unsigned char*)out));
  puts("ASS UTF8 DEFECT: real converter cuts a character at byte 399.");
}`);

writeFileSync('build/audit_subtitle_completion.inc',section(player,'typedef struct { const unsigned char *data;','typedef struct {\n    const char *url, *sid;'));
writeFileSync('build/audit_progressive_functions.inc',section(player,'static int progressive_subtitle_block(','static void progressive_subtitle_stop(ProgressiveSubtitle *stream) {'));
let progressive=read('tools/subtitle_completion_host.c').split('int main(void) {')[0];
progressive=progressive.replace('#include "subtitle_limits.h"','#include "subtitle_limits.h"\n#include "vtt_stream.h"')
  .replace('static int reads,terminal,','static const char *decoded_text="new dialogue";\nstatic int reads,terminal,')
  .replace('static AVSubtitleRect r={SUBTITLE_TEXT,"new dialogue",NULL};','static AVSubtitleRect r={SUBTITLE_TEXT,"new dialogue",NULL};r.text=(char*)decoded_text;')
  .replace('#include "subtitle_completion.inc"','#include "audit_subtitle_completion.inc"');
run('progressive',progressive+String.raw`
typedef struct {ExternalSubtitleStore store;void *mutex;SDL_atomic_t cancel,ready,done;VttStream parser;char url[2048];int result;} ProgressiveSubtitle;
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
  reads=0;terminal=AVERROR_EOF;assert(progressive_subtitle_block(&s,cue,strlen(cue))==1);
  reads=0;decoded_text="m 0 0 l 100 100";
  const char *graphic="00:00:12.000 --> 00:00:14.000\nm 0 0 l 100 100\n";
  int ignored=progressive_subtitle_block(&s,graphic,strlen(graphic));assert(ignored==0);
  printf("GRAPHIC DEFECT: intentionally skipped drawing aborts block, rc=%d.\n",ignored);
  vtt_stream_init(&s.parser,progressive_subtitle_block,&s);attempts=0;stream_mode=0;
  progressive_subtitle_worker(&s);assert(attempts==4&&s.done.value==1&&s.result==-1);
  printf("EXHAUSTED: attempts=%d result=%d partial cues remain=%d.\n",attempts,s.result,external_subtitle_count(&s.store));
  vtt_stream_free(&s.parser);external_subtitle_clear(&s.store);memset(&s,0,sizeof(s));
  vtt_stream_init(&s.parser,progressive_subtitle_block,&s);attempts=0;stream_mode=1;
  progressive_subtitle_worker(&s);assert(attempts==1&&s.result==0&&external_subtitle_count(&s.store)==2);
  puts("CONTRACT LIMIT: early clean HTTP200 is accepted; no completeness marker (not proof the real source is truncated).");
  vtt_stream_free(&s.parser);external_subtitle_clear(&s.store);assert(!io_active&&!formats&&!codecs&&!packets);
}`,['source/subtitle_queue.c','source/subtitle_store.c','source/vtt_stream.c']);
console.log('AUDIT COMPLETE: defects were REPRODUCED, not fixed. Simulated FFmpeg/network/font; no hardware/production/NRO change.');
