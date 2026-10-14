import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const s=readFileSync('source/player.c','utf8').replace(/\r\n/g,'\n');
function part(a,b){const i=s.indexOf(a),j=s.indexOf(b,i);if(i<0||j<i)throw Error('Review discovery extraction');return s.slice(i,j);}
mkdirSync('build',{recursive:true});
writeFileSync('build/subtitle_discovery_open.inc',part('    int native_nsub = nsub;', '    int acur = 0, best_stream ='));
writeFileSync('build/subtitle_discovery_worker.inc',
 part('typedef struct {\n    const char *url, *sid;', 'static int load_external_subtitle(const char *url, ExternalSubtitleStore *store,\n                                  int direct, SDL_atomic_t *cancel) {')+
 part('// Metadata only.', 'static int progressive_subtitle_block'));
const exe=resolve('build/test_subtitle_discovery.exe');
for(const [cmd,args] of [[process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-pthread','-Itools/host-stubs','-Iinclude','-Ibuild','tools/subtitle_discovery_host.c','source/subtitle_catalog.c','source/hot_subtitles.c','source/hls_manifest.c','source/cJSON.c','-lm','-o',exe]],[exe,[]]]){
 const r=spawnSync(cmd,args,{encoding:'utf8',windowsHide:true,timeout:30000});
 if(r.status!==0)throw Error(r.stderr||r.stdout||r.error?.message);process.stdout.write(r.stdout);
}
