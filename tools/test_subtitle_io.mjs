import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
const player=readFileSync('source/player.c','utf8').replace(/\r\n/g,'\n');
const net=readFileSync('source/net.c','utf8').replace(/\r\n/g,'\n');
function section(text,start,end) {
  const a=text.indexOf(start), b=text.indexOf(end,a);
  if(a<0||b<=a) throw Error('Changed boundaries: review subtitle harness');
  return text.slice(a,b);
}
mkdirSync('build',{recursive:true});
writeFileSync('build/subtitle_io_test.inc',
  section(player,'typedef struct { const unsigned char *data;', 'static int subtitle_cancelled')+
  section(net,'typedef struct { struct membuf *out;', 'long net_get_text_limited')+
  section(player,'typedef struct {\n    const char *url, *sid;', 'static int hot_subtitle_worker')+
  section(player,'static int hot_subtitle_worker', 'static int load_external_subtitle(const char *url, ExternalSubtitleStore *store,\n                                  int direct, SDL_atomic_t *cancel) {')+
  section(player,'static int hot_subtitle_wait', 'static int load_hot_subtitle'));
const exe=resolve('build/test_subtitle_io'+(process.platform==='win32'?'.exe':''));
const built=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Itools/host-stubs','-Iinclude','-Ibuild','tools/subtitle_io_host.c','source/hot_subtitles.c','source/cJSON.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(built.status!==0) throw Error(built.stderr||built.error?.message||'compile failed');
const run=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:15000});
if(run.status!==0) throw Error(run.stderr||run.error?.message||'test failed');
process.stdout.write(run.stdout);
