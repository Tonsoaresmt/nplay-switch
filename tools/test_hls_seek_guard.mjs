import {readFileSync,writeFileSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const p=readFileSync('source/player.c','utf8').replace(/\r\n/g,'\n');
const start=p.indexOf('static int seek_video_time('),end=p.indexOf('static int apply_player_seek(',start);
if(start<0||end<start)throw Error('Review native seek guard boundaries');
writeFileSync('build/test_hls_seek_guard.c',String.raw`
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#define AVERROR(n) (-(n))
#define AV_TIME_BASE 1000000
#define AVSEEK_FLAG_BACKWARD 1
typedef int AVFormatContext;
static int calls,response;
static int avformat_seek_file(AVFormatContext*f,int index,int64_t lo,int64_t at,int64_t hi,int flags){
 (void)f;(void)flags;calls++;assert(index==-1&&lo<=at&&hi>=at);return response;
}
static int av_seek_frame(AVFormatContext*f,int index,int64_t at,int flags){
 (void)f;(void)at;(void)flags;calls++;assert(index==-1);return response;
}
static void diag_player_event(const char*a,const char*b,const char*c,...){(void)a;(void)b;(void)c;}
`+p.slice(start,end)+String.raw`
int main(void){
 assert(seek_video_time(NULL,2,1,1800,0,1)==-ENOSYS&&!calls);
 assert(seek_video_time(NULL,2,0,1800,0,1)==0&&calls==1);
 response=-1;calls=0;assert(seek_video_time(NULL,2,0,600,0,1)==-1&&calls==2);
 puts("PASS actual seek helper: HLS never invokes unsafe FFmpeg seek; MP4 retains native seek/fallback with valid bounds.");
}
`);
const exe=resolve('build/test_hls_seek_guard.exe');
for(const [cmd,args] of [[process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','build/test_hls_seek_guard.c','-o',exe]],[exe,[]]]){
 const r=spawnSync(cmd,args,{encoding:'utf8',windowsHide:true,timeout:30000});
 if(r.status!==0)throw Error(r.stderr||r.error?.message);process.stdout.write(r.stdout);
}
