import assert from 'node:assert/strict';
import {readFileSync,existsSync} from 'node:fs';
import {join} from 'node:path';
import vm from 'node:vm';
const main=readFileSync('source/main.c','utf8');
const api=readFileSync('source/api.c','utf8');
assert.ok(main.includes('api_playback_needs_hot_source(&src)'));
assert.ok(main.includes('api_hot_inherit_context(&poll.result.source, out)'));
for(const field of ['newSeasons','topRated']) assert.ok(main.includes(`"${field}"`));
assert.ok(!main.includes('ui_card_badge("Pronto"'));
assert.ok(!main.includes('  |  Pronto pra tocar'));
assert.ok(api.includes('!strcasecmp(container, "hls")'));
const root=process.env.NPLAY_BACKEND_ROOT || 'C:/iptv';
const path=join(root,'src/routes/hot-stream.js');
if(!existsSync(path)){
 console.log('SITE COMPAT: internal UI guards passed; backend descriptor comparison unavailable.');
 process.exit(0);
}
const source=readFileSync(path,'utf8').replace(/\r\n/g,'\n');
const start=source.indexOf('export function debridPlaybackResponse(');
const end=source.indexOf('// Legenda de sess',start);
// The comment is Portuguese in the canonical source.
const utfEnd=source.indexOf('// Legenda de sessão',start);
const finish=end>=0 ? end : utfEnd;
assert.ok(start>=0 && finish>start,'Review descriptor extraction boundaries');
let hlsJobs=0;
const descriptor=vm.runInNewContext(source.slice(start,finish).replace('export function','function')+'\ndebridPlaybackResponse',{
 createDebridSession:direct=>({...direct,id:'1234567890_fixture_session'}),
 privateMediaUrl:(kind,id,account,tail='video')=>`/api/media/${kind}/${id}/${tail}?token=fixture`,
 debridHlsAvailable:()=>true,ensureDebridHls:()=>{hlsJobs++;return {};},
 debridSessionProbe:()=>Promise.resolve({ok:true}), refreshDebridSessionLink:()=>{},
});
const mp4=descriptor({needsRemux:false,fileName:'movie.mp4',fileSize:10737418240},91,7,42,null,{id:1});
assert.equal(mp4.seek_mode,'range');assert.equal(mp4.seekable,true);
assert.equal(mp4.request_id,91);assert.equal(typeof mp4.session_id,'string');
const mkv=descriptor({needsRemux:true,fileName:'movie.mkv',fileSize:10737418240},91,7,42,null,{id:1});
assert.equal(mkv.seek_mode,'restart');assert.equal(mkv.seekable,false);assert.equal(hlsJobs,0);
if(source.slice(start,finish).includes('tvHls')){
 const hls=descriptor({needsRemux:true,fileName:'movie.mkv',fileSize:10737418240},91,7,42,null,{id:1},{hls:true});
 assert.equal(hls.container,'hls');assert.equal(hls.seekable,true);assert.equal(hls.seek_mode,'range');
 assert.match(hls.play_url,/\/hls\/index\.m3u8\?token=fixture$/);assert.equal(hlsJobs,1);
 console.log('SITE COMPAT: deployed descriptor function extracted; MP4 Range, MKV restart, TV HLS and request/SID contracts passed (provider/jobs mocked).');
}else console.log('SITE COMPAT: legacy descriptor MP4/MKV contract passed; backend has no TV-HLS descriptor.');
