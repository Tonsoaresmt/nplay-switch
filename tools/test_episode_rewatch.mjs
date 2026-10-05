import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const main = readFileSync('source/main.c','utf8').replace(/\r\n/g,'\n');
function section(startMark,endMark,from=0) {
  const start=main.indexOf(startMark,from), end=main.indexOf(endMark,start);
  if(start<0||end<0) throw Error(`Review extraction: ${startMark}`);
  return main.slice(start,end);
}
const functions = section('static int episode_completed(cJSON *episode) {','static void begin_catalog_fetch_mode(')+
  section('static int choose_next_episode(', '// Monta a lista do painel Episodios',main.indexOf('// Return the episode to play, or zero when'))+
  section('static void play_episodes_build(', 'static void play_history_item(',main.indexOf('// Monta a lista do painel Episodios'));
mkdirSync('build',{recursive:true});
writeFileSync('build/audit_rewatch_actual.inc',functions);
const cc=process.env.HOST_CC || 'gcc';
const exe=resolve('build/audit_rewatch_sequence'+(process.platform==='win32'?'.exe':''));
let r=spawnSync(cc,['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','-Ibuild','tools/episode_rewatch_host.c','source/episode_flow.c','source/audio_policy.c','source/cJSON.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0) throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:10000});
if(r.status!==0) throw Error(r.stdout+'\n'+(r.stderr||r.error?.message));
process.stdout.write(r.stdout);
