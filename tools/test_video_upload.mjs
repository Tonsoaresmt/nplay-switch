// Actual upload/conversion C, instrumented FFmpeg/SDL resource boundaries.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
let player=readFileSync('source/player.c','utf8');
if(process.argv.includes('--baseline-056')) {
 const r=spawnSync('C:/Program Files/Git/cmd/git.exe',['show','4a53f67:source/player.c'],{encoding:'utf8',windowsHide:true});
 if(r.status!==0)throw Error(r.stderr);player=r.stdout;
}
player=player.replace(/\r\n/g,'\n');
mkdirSync('build',{recursive:true});
const start=player.indexOf('                    AVFrame *u = frame;'),end=player.indexOf('                    have_video_frame = 1;',start);
if(start<0||end<start)throw Error('Review frame upload extraction');
writeFileSync('build/video_upload.inc',player.slice(start,end));
const exe=resolve('build/test_video_upload.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','-Ibuild','tools/video_upload_host.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:20000});
process.stdout.write(r.stdout||'');if(r.status!==0)throw Error(r.stderr||r.error?.message||'upload failed');
