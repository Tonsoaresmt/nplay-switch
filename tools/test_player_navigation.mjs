import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const read=p=>readFileSync(p,'utf8').replace(/\r\n/g,'\n');
const main=read('source/main.c'),player=read('source/player.c');
function section(s,a,b){const start=s.indexOf(a),end=s.indexOf(b,start);if(start<0||end<start)throw Error(a);return s.slice(start,end);}
mkdirSync('build',{recursive:true});
writeFileSync('build/navigation_types.inc',section(main,'typedef struct {\n    int valid, rail,','static int hero_count('));
writeFileSync('build/navigation_landing.inc',section(main,'static void add_rail(', 'static const char *landing_path(')+
 section(main,'static void load_landing(int tab) {','static void landing_invalidate('));
writeFileSync('build/navigation_enter.inc',section(main,'static void enter_tab(int tab) {','static void input_landing('));
writeFileSync('build/navigation_touch.inc',section(player,'            if ((timeline_seek || episodes_menu) &&','            if (e.type == SDL_FINGERDOWN && track_menu) {'));
writeFileSync('build/navigation_preview.inc',section(player,'                    // A drag previews one target;', '                } else if (!sequential_stream && ty >= 635'));
const exe=resolve('build/test_player_navigation.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','-Ibuild',
 'tools/player_navigation_host.c','source/cJSON.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:10000});process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message);
for(const marker of ['landing_capture_focus();\n    g_tab = tab;', 'static void playback_memory_enter(void) {\n    landing_capture_focus();'])
 if(!main.includes(marker))throw Error('Missing context capture entry: '+marker);
