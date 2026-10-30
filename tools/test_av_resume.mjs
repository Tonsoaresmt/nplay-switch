// Execute actual PCM acceptance/queue block; resampler/device/time simulated.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
let p=readFileSync('source/player.c','utf8').replace(/\r\n/g,'\n');
if(process.argv.includes('--baseline-057')){
 const r=spawnSync('C:/Program Files/Git/cmd/git.exe',['show','3f0bae7:source/player.c'],{encoding:'utf8',windowsHide:true});
 if(r.status!==0)throw Error(r.stderr);p=r.stdout.replace(/\r\n/g,'\n');
}
const start=p.indexOf('                        double audio_pts = ats != AV_NOPTS_VALUE');
const end=p.indexOf('\n                    }\n                }\n            }\n        } else if (pkt->stream_index == vidx)',start);
if(start<0||end<start)throw Error('Review audio queue extraction');
mkdirSync('build',{recursive:true});writeFileSync('build/av_resume_queue.inc',p.slice(start,end));
const helper=p.indexOf('static int player_queue_pcm('),helperEnd=p.indexOf('static int create_audio_dec(',helper);
writeFileSync('build/av_resume_helper.inc',helper>=0?p.slice(helper,helperEnd):'');
const scheduling=p.indexOf('        int take = 0;',p.indexOf('Uint32 audio_before_ms'));
const schedulingEnd=p.indexOf('        int ret = take',scheduling);
writeFileSync('build/av_resume_schedule.inc',p.slice(scheduling,schedulingEnd));
const exe=resolve('build/test_av_resume.exe');
let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','-Ibuild','tools/av_resume_host.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
if(r.status!==0)throw Error(r.stderr||r.error?.message);
const modes=process.argv.includes('--baseline-057')?[process.argv.includes('--late')?'late':'preroll']:['preroll','late','matrix'];
for(const mode of modes){
 r=spawnSync(exe,[mode],{encoding:'utf8',windowsHide:true,timeout:10000});
 process.stdout.write(r.stdout||'');if(r.status!==0)throw Error(r.stderr||r.error?.message||'A/V regression');
}
if(!process.argv.includes('--baseline-057')&&p.includes('audio_clock = cur_pos;'))throw Error('Modal overwrites queued audio end PTS');
if(!process.argv.includes('--baseline-057')){
 if(p.includes('queue_during_resume'))throw Error('Preroll silently drops decoded audio again');
 if((p.match(/player_audio_timeline_reset\(&audio_timing,/g)||[]).length!==5)throw Error('Missing initial/seek/track audio reset');
 const video=p.slice(p.indexOf('        } else if (pkt->stream_index == vidx)'),p.indexOf('        } else if (scur >= 0 && !SUB_EXTERNAL(scur)'));
 if(video.indexOf('if (resume_first_frame && adev && player_audio_ready(paused, logged_first_present, resume_preroll)) SDL_PauseAudioDevice(adev, 0);')<video.indexOf('SDL_RenderPresent(ren);'))throw Error('Seek unpauses sound before presentation');
 if(p.includes('if (adev && !paused) SDL_PauseAudioDevice(adev, 0);'))throw Error('Menu bypasses audio preroll gate');
}
