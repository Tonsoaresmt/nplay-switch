// Diagnostic ONLY: exit 0 means the documented defects were reproduced.
// Never add this negative audit to validate_release as a positive test.
import {readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
if (!process.argv.includes('--reproduce-058')) throw Error('Use --reproduce-058; this is a negative audit, not release approval.');
mkdirSync('build', {recursive:true});
const baseline=spawnSync('C:/Program Files/Git/cmd/git.exe',['show','93c4527:source/player.c'],{encoding:'utf8',windowsHide:true});
if(baseline.status!==0)throw Error('Cannot load published .58 baseline');
const p=baseline.stdout.replace(/\r\n/g,'\n');
function section(s,a,b) {const i=s.indexOf(a),j=s.indexOf(b,i);if(i<0||j<=i)throw Error(`Review extraction ${a}`);return s.slice(i,j);}
const worker=section(p,'typedef struct {\n    AVPacket *packet;','// A thread de UI permanece viva')+
  section(p,'static void demux_worker_stop(','static int player_prepare_restart');
writeFileSync('build/demux_worker_test.inc',worker);
const schedule=section(p,'        int take = 0;\n        int video_first', '        int ret = take');
writeFileSync('build/torbox_schedule_audit.inc',schedule);
let host=section(readFileSync('tools/demux_worker_host.c','utf8').replace(/\r\n/g,'\n'),'#define _POSIX_C_SOURCE','int main(void)');
const old='p->stream_index = audio_every > 0 && n % audio_every == audio_every - 1 ? 1 : 0;';
if(!host.includes(old))throw Error('Review av_read_frame shim');
host=host.replace(old,'p->stream_index = n < 600 ? 1 : 0;');
host += `
#include "player_audio_timeline.h"
int main(void) {
    (void)audio_every; (void)barrier; (void)demux_worker_clear;
    (void)demux_worker_resume; (void)demux_worker_snapshot;
    (void)host_worker_calloc; (void)host_worker_free;
    AVCodecParameters vc={AVMEDIA_TYPE_VIDEO}, ac={2};
    AVStream vs={&vc,1.0/30}, as={&ac,1.0/48000};
    AVStream *streams[]={&vs,&as}; AVFormatContext fmt={streams};
    PlayerOpenDeadline watch={0}; DemuxWorker demux;
    AVPacket out, *pkt=&out; Uint32 read_ms=0;
    int adev=1,vidx=0,aidx=1,actx=1,paused=0,refill=0,resume_preroll=1;
    unsigned audio_before_ms=400;
    packet_bytes=1000; assert(!demux_worker_start(&demux,&fmt,&watch,1));
    wait_count(&demux,DEMUX_QUEUE_PACKETS);
    int before=atomic_load(&reads),taken=0;
    for(int i=0;i<1000;i++) {
#include "torbox_schedule_audit.inc"
        taken+=take;
    }
    assert(!taken && occupancy(&demux)==DEMUX_QUEUE_PACKETS);
    assert(!demux_worker_terminal(&demux) && atomic_load(&reads)==before);
    printf("REPRODUCED .58 video-first circular wait: %d compressed audio packets, video not yet read; 1000 polls consumed=%d, source reads=%d (unchanged).\\n",DEMUX_QUEUE_PACKETS,taken,before);
    demux_worker_stop(&demux); assert(!atomic_load(&live_packets));
    puts("Synthetic packet ordering; NOT proof that the reported TorBox title has this ordering.");
    return 0;
}
`;
writeFileSync('build/torbox_demux_audit.c',host);
const patch=readFileSync('build/audit-devkitpro-tls.patch');
if(createHash('sha256').update(patch).digest('hex')!=='57ea7ec8ed26d13d3172d0fd589b7883d22f0c180d50b7434fcc73fb2b3ab7d7')throw Error('TLS patch differs from pinned SDK recipe');
const added=patch.toString('utf8').split(/\r?\n/).filter(l=>l.startsWith('+')&&!l.startsWith('+++')).map(l=>l.slice(1)).join('\n');
let tls=section(added,'typedef struct TLSContext {','static int tls_write(');
const hostnamePatch=readFileSync('tools/ffmpeg-libnx-tls-hostname.patch','utf8');
if(!hostnamePatch.includes('+        if (shr->verify) verifyopt |= SslVerifyOption_PeerCa | SslVerifyOption_HostName;'))throw Error('Review local TLS verification patch');
tls=tls.replace('if (shr->verify) verifyopt |= SslVerifyOption_PeerCa;','if (shr->verify) verifyopt |= SslVerifyOption_PeerCa | SslVerifyOption_HostName;');
writeFileSync('build/torbox_tls_audit.inc',tls);
const cc=process.env.HOST_CC||'gcc';
for(const [name,source] of [['demux','build/torbox_demux_audit.c'],['tls','tools/torbox_tls_audit_host.c']]){
 const exe=resolve(`build/audit_torbox_${name}.exe`);
 const externalWarnings=name==='tls'?['-Wno-parentheses']:[]; // Pinned upstream while(rc=...) is intentional.
 let r=spawnSync(cc,['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',...externalWarnings,'-pthread','-Iinclude','-Ibuild',source,'-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
 if(r.status!==0)throw Error(`compile ${name}: ${r.stderr||r.error?.message||r.status}`);
 r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:10000});
 process.stdout.write(r.stdout||'');if(r.status!==0)throw Error(`audit ${name}: ${r.stderr||r.error?.message||r.status}`);
}
console.log('NEGATIVE AUDIT COMPLETE: defects reproduced, NOT release validation. No NRO or backend changed.');
