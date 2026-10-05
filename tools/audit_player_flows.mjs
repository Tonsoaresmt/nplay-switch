// Diagnostic only: success means the documented defects were reproduced.
// Extracts current C functions; API, SDL and catalog fixtures are simulated.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const read=p=>readFileSync(p,'utf8').replace(/\r\n/g,'\n');
const main=read('source/main.c'),player=read('source/player.c');
function section(s,a,b) {
  const start=s.indexOf(a),end=s.indexOf(b,start);
  if(start<0||end<start)throw Error(`Review extraction boundary: ${a}`);
  return s.slice(start,end);
}
mkdirSync('build',{recursive:true});
writeFileSync('build/flow_history.inc',section(main,'static void pump_history(void) {','static void apply_downloads('));
writeFileSync('build/flow_landing.inc',section(main,'static void landing_apply(int tab,','static const char *landing_path(')+
  section(main,'static void load_landing(int tab) {','static void landing_invalidate('));
writeFileSync('build/flow_finalize.inc',section(main,'static int finalize_natural_playback(','static int on_player_heartbeat('));
const terminal=section(player,'        if (ret < 0) {  // fim real','        if (buffering_since) {');
writeFileSync('build/flow_terminal.inc',terminal);
const cc=process.env.HOST_CC||'gcc',exe=resolve('build/audit_player_flows.exe');
let r=spawnSync(cc,['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','-Ibuild',
  'tools/audit_player_flows_host.c','source/cJSON.c','-o',exe],
  {encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:15000});
process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message||'Failed to reproduce');

// Inspections complement C simulations; these are not graphics/FFmpeg tests.
const touch=section(player,'            } else if (e.type == SDL_FINGERDOWN) {','        if (!running) break;');
if(!touch.includes('if (!have_video_frame || track_menu || timeline_seek) continue;')||
   touch.includes('episodes_menu')||touch.includes('episodes_sel'))
  throw Error('Review episode/timeline touch finding: implementation changed');
console.log('INSPECTED: generic touch ignores timeline controls and has no episode-panel hit test.');
const internal=section(player,'static int player_play_internal(', 'static int playback_heartbeat_thread(');
const header=read('include/player.h');
if(!internal.includes('int running = 1, paused = 0,')||/int (start_paused|resume_paused);/.test(header))
  throw Error('Review pause-on-reopen finding: implementation changed');
console.log('INSPECTED: internal attempts start paused=0; request/result do not carry paused intent.');

// Use the existing actual pthread/fetch fixture with a deliberately slow
// cooperative cancellation. The 500 ms delay is injected, not a Wi-Fi metric.
const fetchBegin=player.indexOf('typedef struct {\n    char url[HLS_MANIFEST_URI_MAX];');
const fetchEnd=player.indexOf('static int subtitle_session_take(',fetchBegin);
if(fetchBegin<0||fetchEnd<fetchBegin)throw Error('Review fetch extraction');
writeFileSync('build/subtitle_fetch.inc',player.slice(fetchBegin,fetchEnd));
let slow=read('tools/subtitle_fetch_host.c').replace('#include <unistd.h>', '#include <unistd.h>\n#include <time.h>');
slow=slow.replace('static SDL_atomic_t ready, video_abort;', 'static SDL_atomic_t ready, video_abort, entered;');
slow=slow.replace('while (!SDL_AtomicGet(&ready) && !SDL_AtomicGet(cancel)) usleep(1000);',
  'SDL_AtomicSet(&entered, 1); usleep(500000);\n    while (!SDL_AtomicGet(&ready) && !SDL_AtomicGet(cancel)) usleep(1000);');
slow=slow.replace('static void reset_load(void) {', 'static void reset_load(void) { SDL_AtomicSet(&entered, 0);');
const stop='subtitle_fetch_stop(&fetch); assert(!fetch.thread && *applied.text == 80);';
if(!slow.includes(stop))throw Error('Review cancellation latency fixture');
slow=slow.replace(stop, `while(!SDL_AtomicGet(&entered)) usleep(1000);
    struct timespec before,after;clock_gettime(CLOCK_MONOTONIC,&before);
    ${stop}
    clock_gettime(CLOCK_MONOTONIC,&after);
    double elapsed=(after.tv_sec-before.tv_sec)*1000.0+(after.tv_nsec-before.tv_nsec)/1000000.0;
    assert(elapsed>=400);
    printf("REPRODUCED cancel/join blocks caller %.0f ms for an injected 500-ms worker delay (not hardware measurement).\\n",elapsed);`);
writeFileSync('build/audit_flow_cancel.c',slow);
const cancelExe=resolve('build/audit_flow_cancel.exe');
r=spawnSync(cc,['-std=c11','-Wall','-Wextra','-Werror','-pthread','-Itools/host-stubs','-Iinclude','-Ibuild',
  'build/audit_flow_cancel.c','-o',cancelExe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(cancelExe,[],{encoding:'utf8',windowsHide:true,timeout:15000});
process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message);
console.log('AUDIT REPRODUCED; not a release acceptance test. No NRO/backend/release changed.');
