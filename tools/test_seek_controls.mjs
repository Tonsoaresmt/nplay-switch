import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const read=p=>readFileSync(p,'utf8').replace(/\r\n/g,'\n');
const player=read('source/player.c'),store=read('source/store.c'),main=read('source/main.c');
function extract(s,a,b){const x=s.indexOf(a),y=s.indexOf(b,x);if(x<0||y<x)throw Error(a);return s.slice(x,y);}
mkdirSync('build',{recursive:true});
writeFileSync('build/seek_poll.inc',extract(player,'            int local_seek_button = -1;','            // O painel e uma superficie direta'));
writeFileSync('build/seek_step.inc','if (0) {} '+extract(player,'                else if (b == JOY_R || b == JOY_L || b == JOY_ZR || b == JOY_ZL) {','                else if (b == JOY_Y)'));
writeFileSync('build/seek_cancel.inc',extract(player,'                if (quick_seek_active && b == JOY_B) {','                if (next_selected) {'));
writeFileSync('build/seek_store.inc',extract(store,'static int read_positive_id(', 'int store_load_profile_id(')+
 extract(store,'int store_load_seek_stick(', 'int store_load_player_volume('));
writeFileSync('build/seek_pref.inc',extract(main,'static void save_selected_preference(int direction) {','    // An older account GET'));
for(const marker of ['SDL_JoystickGetAxis(joy, 2)', 'player_seek_stick_arm(req->right_stick_seek',
 'if (quick_seek_active && !player_seek_button(b)) quick_seek_active = 0;',
 'e.jbutton.which != SDL_JoystickInstanceID(joy)'])if(!player.includes(marker))throw Error('Missing guard: '+marker);
if(player.includes('SDL_PushEvent(&commit)')||player.includes('SDL_JoystickGetAxis(joy, 0)'))throw Error('Unsafe/left-stick seek returned');
if((main.match(/req.right_stick_seek = g_pref_seek_stick;/g)||[]).length!==2)throw Error('Missing one playback entry');
if((main.match(/g_pref_seek_stick = store_load_seek_stick\(\);/g)||[]).length!==2)throw Error('Missing one profile entry');
const exe=resolve('build/test_seek_controls.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','-Ibuild',
 'tools/seek_controls_host.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
mkdirSync('build/seek-store',{recursive:true});
r=spawnSync(exe,[],{cwd:resolve('build/seek-store'),encoding:'utf8',windowsHide:true,timeout:10000});
process.stdout.write(r.stdout||'');if(r.status!==0)throw Error(r.stderr||r.error?.message);
