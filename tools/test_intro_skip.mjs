import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const read=p=>readFileSync(p,'utf8').replace(/\r\n/g,'\n');
const player=read('source/player.c'),ui=read('source/player_ui.c');
const section=(s,a,b)=>{const i=s.indexOf(a),j=s.indexOf(b,i);if(i<0||j<i)throw Error(a);return s.slice(i,j);};
mkdirSync('build',{recursive:true});
writeFileSync('build/intro_touch.inc',section(player,'            // Pular abertura is','            if (e.type == SDL_QUIT) running = 0;'));
writeFileSync('build/intro_activate.inc',section(player,'                if (intro_selected &&','                if (timeline_seek) {'));
writeFileSync('build/intro_draw.inc',section(ui,'static void draw_intro_skip(', 'void pui_draw('));
for(const marker of ['intro_skip_stop(&intro_skip);','intro_skip_poll(&intro_skip, req->item_id, req->source_id, dur, g_token)',
 '!base->intro_visible','timeline_seek_was_paused = paused;','timeline_seek_target = intro_skip_target(skip_range, dur);'])
 if(!player.includes(marker))throw Error(marker);
if(!ui.includes('bottom = INTRO_BUTTON_Y - 12;')||!ui.includes('draw_intro_skip(r, h);'))throw Error('Missing subtitle reserve/draw');
const exe=resolve('build/test_intro_skip.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-pthread','-Itools/host-stubs','-Iinclude','-Ibuild',
 'tools/intro_skip_host.c','source/intro_skip.c','source/cJSON.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:30000});process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message||`exit ${r.status}`);
