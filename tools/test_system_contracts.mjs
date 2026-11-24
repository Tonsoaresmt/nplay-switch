// Positive counterparts of the .62 HLS/late-choice/URL/budget audit cases.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const p=readFileSync('source/player.c','utf8').replace(/\r\n/g,'\n');
const section=(a,b,from=0)=>{const i=p.indexOf(a,from),j=p.indexOf(b,i);if(i<0||j<i)throw Error(a);return p.slice(i,j);};
mkdirSync('build',{recursive:true});
const save=(name,s)=>writeFileSync(`build/audit062_${name}.inc`,s);
save('master',section('typedef struct { char name[96];','static const char *master_audio_norm')+section('static void master_audio_info(', '// Escolhe pelo master'));
save('master_map',section('    for (int i = 0; i < count; i++) {','    free(parsed);',p.indexOf('static int player_hls_choose_audio')));
save('discovery_retry',section('static int subtitle_discovery_retryable(', 'static int progressive_subtitle_block('));
save('discovery_apply',section('        if (subtitle_discovery_poll(discovery)) {','        if (!discovery && discovery_retry'));
const exe=resolve('build/test_system_tracks.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-DCORRECTED=1','-Iinclude','-Ibuild','tools/audit_062_tracks_host.c','source/audio_policy.c','source/hls_manifest.c','source/subtitle_choice.c','source/subtitle_catalog.c','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:15000});if(r.status!==0)throw Error(r.stderr||r.error?.message);
console.log('PASS corrected HLS accessibility and forced->full automatic upgrade; prior applied track preserved until replacement. Network/render simulated.');
const net=readFileSync('source/net.c','utf8').replace(/\r\n/g,'\n');
const writer=net.indexOf('static size_t write_cb('),writerEnd=net.indexOf('struct file_download_ctx',writer);
if(writer<0||writerEnd<writer||!net.includes('if (!out->limit) out->limit = response_body_budget(url);'))throw Error('Response cap not installed in real transport');
save('writer',net.slice(writer,writerEnd));
for(const [name,sources]of[
 ['budget',['tools/system_budget_host.c']],
 ['descriptor',['tools/audit_062_descriptor_host.c','source/api.c','source/playback_restart.c','source/hot_subtitles.c','source/hls_manifest.c','source/cJSON.c']]
]){
 const bin=resolve(`build/test_system_${name}.exe`);
 r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-DCORRECTED=1','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Itools/host-stubs','-Iinclude','-Ibuild',...sources,'-lm','-o',bin],{encoding:'utf8',windowsHide:true,timeout:30000});
 if(r.status!==0)throw Error(r.stderr||r.error?.message);
 r=spawnSync(bin,[],{encoding:'utf8',windowsHide:true,timeout:15000});process.stdout.write(r.stdout||'');if(r.status!==0)throw Error(r.stderr||r.error?.message);
}
