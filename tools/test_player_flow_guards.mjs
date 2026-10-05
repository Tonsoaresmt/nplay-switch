// Positive regressions: current real C control, mocked UI/network/decoder.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const read=p=>readFileSync(p,'utf8').replace(/\r\n/g,'\n');
const main=read('source/main.c'),player=read('source/player.c');
function section(s,a,b){const start=s.indexOf(a),end=s.indexOf(b,start);if(start<0||end<start)throw Error(a);return s.slice(start,end);}
mkdirSync('build',{recursive:true});
writeFileSync('build/flow_guards_history.inc',section(main,'static int history_index_for_id(', 'static void apply_downloads('));
writeFileSync('build/flow_guards_finalize.inc',section(main,'static int finalize_natural_playback(', 'static int on_player_heartbeat('));
writeFileSync('build/flow_guards_terminal.inc',section(player,'        if (ret < 0) {  // fim real', '        if (buffering_since) {'));
const exe=resolve('build/test_player_flow_guards.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','-Ibuild',
  'tools/player_flow_guards_host.c','source/cJSON.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:10000});process.stdout.write(r.stdout||'');
if(r.status!==0)throw Error(r.stderr||r.error?.message);
if(!player.includes('avcodec_send_packet(vctx, drain_decoder ? NULL : pkt)')||
   !player.includes('avcodec_send_packet(actx, drain_decoder ? NULL : pkt)'))throw Error('Decoder drain routing missing');
