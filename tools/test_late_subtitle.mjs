// Execute the real late progressive publication/cancel block; worker/device fake.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const p=readFileSync('source/player.c','utf8').replace(/\r\n/g,'\n');
const a=p.indexOf('        // Late remux tracks start'),b=p.indexOf('        if (!subtitle_fetch.thread && !pending_hot_subtitle)',a);
if(a<0||b<a)throw Error('Review late subtitle extraction');
mkdirSync('build',{recursive:true});writeFileSync('build/late_subtitle_poll.inc',p.slice(a,b));
const start=p.indexOf('static ProgressiveSubtitle *start_hot_subtitle('),end=p.indexOf('static int load_hot_subtitle(',start);
if(start<0||end<start)throw Error('Review progressive factory extraction');
writeFileSync('build/late_subtitle_factory.inc',p.slice(start,end));
const retry=p.indexOf('        if (sub_result != 0) {'),retryEnd=p.indexOf('\n    }\n    g_player_subtitle_index',retry);
if(retry<0||retryEnd<retry)throw Error('Review initial hot retry extraction');
writeFileSync('build/late_subtitle_initial.inc',p.slice(retry,retryEnd));
const exe=resolve('build/test_late_subtitle.exe');
for(const [cmd,args]of[[process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','-Ibuild','tools/late_subtitle_host.c','-o',exe]],[exe,[]]]){
 const r=spawnSync(cmd,args,{encoding:'utf8',windowsHide:true,timeout:30000});
 if(r.status!==0)throw Error(r.stderr||r.error?.message);process.stdout.write(r.stdout||'');
}
