// Negative protocol audit: real localhost HTTP abort + SQLite, backend SQL exact.
// No production connection/account/device used. exit0 is a reproduced hazard.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createServer} from 'node:http';
import {DatabaseSync} from 'node:sqlite';
if(!process.argv.includes('--reproduce-062'))throw Error('Use --reproduce-062: negative protocol audit, not release approval');
const backend='1e59abdf37648141b52a6fe6c1adf9574731da64';
const source=spawnSync('C:/Program Files/Git/cmd/git.exe',['show',`${backend}:src/routes/stream.js`],{
 cwd:'C:/NplaySwitch/.codex-tmp/backend-device-identity',encoding:'utf8',windowsHide:true});
assert.equal(source.status,0,source.stderr);
const section=source.stdout.slice(source.stdout.indexOf("app.post('/stream/:itemId/stop'"));
const sql=section.match(/'(UPDATE playback_sessions SET active=0[^']+)'/)?.[1];
assert(sql&&sql.includes('device_id IS ?')&&!sql.includes('id=? AND user_id'));
const db=new DatabaseSync(':memory:');
db.exec('CREATE TABLE playback_sessions(id INTEGER PRIMARY KEY,user_id INTEGER,item_id INTEGER,device_id INTEGER,active INTEGER)');
const insert=db.prepare('INSERT INTO playback_sessions VALUES(?,1,42,?,1)');
insert.run(10,7);
let markArrived,releaseStop,finishStop;
const arrived=new Promise(r=>markArrived=r),gate=new Promise(r=>releaseStop=r),finished=new Promise(r=>finishStop=r);
const server=createServer(async(req,res)=>{
 if(req.url!=='/old-stop'){res.writeHead(404);res.end();return;}
 req.resume();markArrived();await gate;
 // Models a handler delayed before its write. Client abort does not undo a
 // handler already accepted by the server, nor fence a later playback session.
 db.prepare(sql).run(1,42,7);finishStop();res.end('{"ok":true}');
});
server.listen(0,'127.0.0.1');await new Promise(r=>server.once('listening',r));
try{
 const abort=new AbortController();
 const response=fetch(`http://127.0.0.1:${server.address().port}/old-stop`,{method:'POST',body:'{}',signal:abort.signal,headers:{connection:'close'}}).then(()=>false,e=>e.name==='AbortError');
 await arrived;abort.abort();assert(await response);
 insert.run(11,7);insert.run(12,8);releaseStop();await finished;
 const state=db.prepare('SELECT id,active FROM playback_sessions ORDER BY id').all();
 assert.deepEqual(state.map(r=>r.active),[0,0,1]);
 console.log('REPRODUCED conditional stop race: after real HTTP abort, delayed current backend SQL closes old AND newer playback on same item/device; another device remains active. Actual production delay/order NOT measured.');
 db.exec('UPDATE playback_sessions SET active=1');
 db.prepare('UPDATE playback_sessions SET active=0 WHERE id=? AND user_id=? AND item_id=? AND device_id IS ?').run(10,1,42,7);
 assert.deepEqual(db.prepare('SELECT active FROM playback_sessions ORDER BY id').all().map(r=>r.active),[0,1,1]);
 console.log('MODEL COUNTERCHECK: adding exact session identity fences the older stop; hypothetical SQL only, no backend/app implementation or deployment.');
}finally{
 releaseStop();server.closeAllConnections();await new Promise(r=>server.close(r));db.close();
}
