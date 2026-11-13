// Actual candidate route + helper, real in-memory SQLite/transactions.
// Fastify authentication/billing/JWT/QR are dependency stubs, not production.
import {readFileSync} from 'node:fs';
import {DatabaseSync} from 'node:sqlite';
import {randomBytes} from 'node:crypto';
import assert from 'node:assert/strict';
import {join} from 'node:path';
const read=p=>readFileSync(p,'utf8').replace(/\r/g,'');
const base='integration/nplay-device-identity/';
const backendRoot=process.env.NPLAY_BACKEND_ROOT||'build/site-c9d0f77';
const strip=s=>s.replace(/^import .*;\n/gm,'').replace(/export default /g,'').replace(/export function /g,'function ');
function setup(legacy=false){
 const db=new DatabaseSync(':memory:');db.exec(`PRAGMA foreign_keys=ON;
 CREATE TABLE users(id INTEGER PRIMARY KEY,username TEXT,role TEXT,max_devices INT,max_sessions INT,device_limit_override INT,session_limit_override INT);
 CREATE TABLE devices(id INTEGER PRIMARY KEY,user_id INT REFERENCES users(id),name TEXT,type TEXT,fingerprint TEXT,approved INT,last_ip TEXT,last_seen_at TEXT,created_at TEXT DEFAULT CURRENT_TIMESTAMP);
 CREATE TABLE device_pairings(id INTEGER PRIMARY KEY,user_code TEXT UNIQUE,device_code TEXT UNIQUE,device_name TEXT,device_type TEXT,status TEXT DEFAULT 'pending',user_id INT REFERENCES users(id),device_id INT REFERENCES devices(id) ON DELETE SET NULL,ip TEXT,expires_at TEXT,created_at TEXT DEFAULT CURRENT_TIMESTAMP);
 INSERT INTO users VALUES(1,'Fixture A','user',1,1,NULL,NULL),(2,'Fixture B','user',1,1,NULL,NULL);`);
 const get=(s,p=[])=>db.prepare(s).get(...p),all=(s,p=[])=>db.prepare(s).all(...p),run=(s,p=[])=>db.prepare(s).run(...p);
 const tx=fn=>{db.exec('BEGIN IMMEDIATE');try{const r=fn();db.exec('COMMIT');return r;}catch(e){db.exec('ROLLBACK');throw e;}};
 const routes=new Map(),invalidated=[];
 const app={authenticate(){},jwt:{sign(payload){return {fixture:true,...payload};}},
  post(path,...args){if(path==='/approve')assert.equal(args[0].preHandler[0],this.authenticate);routes.set('POST '+path,args.at(-1));},
  get(path,...args){routes.set('GET '+path,args.at(-1));},delete(path,...args){routes.set('DELETE '+path,args.at(-1));}};
 const source=legacy?read(join(backendRoot,'src/routes/devices.js')):
  read(base+'pairing-device.js')+'\n'+read(base+'devices.js');
 const factory=new Function('get','all','run','tx','randomBytes','config','withBillingLimits','hit','pairingUrl','pairingMatrix','invalidateAuthCache',
  strip(source)+";deviceRoutes.testMigrate=typeof installPairingIdentitySchema==='function'?installPairingIdentitySchema:null;return deviceRoutes;");
 const install=factory(get,all,run,tx,randomBytes,{publicUrl:'https://nplay.test'},x=>x,()=>({allowed:true}),()=>'',()=>({encoding:'fixture'}),id=>invalidated.push(id));
 const ready=install(app);
 async function call(method,path,body={},account=null){await ready;
  const reply={status:200,headers:{},body:undefined,code(c){this.status=c;return this;},header(k,v){this.headers[k]=v;return this;},send(b){this.body=b;return this;}};
  const req={body,account,ip:'192.0.2.1',params:{id:body.id}};
  const result=await routes.get(method+' '+path)(req,reply);return {status:reply.status,body:reply.body??result};
 }
 const passwordSource=read(join(backendRoot,'src/routes/auth.js'));
 const start=passwordSource.indexOf('function resolveDevice('),end=passwordSource.indexOf('export default async function authRoutes',start);
 assert(start>=0&&end>start);
 const passwordResolve=new Function('get','run','all',passwordSource.slice(start,end)+';return resolveDevice;')(get,run,all);
 return {db,get,run,all,call,ready,invalidated,passwordResolve,migrate:install.testMigrate};
}
const fp='nplay-switch-0123456789abcdef0123456789abcdef';
const fp2='nplay-switch-ffffffffffffffffffffffffffffffff';
const account={id:1,max_devices:1};
const code=async(s,fingerprint=fp)=>s.call('POST','/code',{device_name:'Nintendo Switch',device_type:'console',device_fingerprint:fingerprint});
const approve=async(s,c,a=account)=>s.call('POST','/approve',{user_code:c.body.user_code},a);
const poll=async(s,c)=>s.call('POST','/token',{device_code:c.body.device_code});
function add(s,user=1,key=fp,name='Nintendo Switch'){return Number(s.run('INSERT INTO devices(user_id,name,type,fingerprint,approved) VALUES(?,?,?,?,1)',[user,name,'console',key]).lastInsertRowid);}
{
 const s=setup(true);await s.ready;add(s);
 const c=await code(s);assert.equal((await approve(s,c)).status,409);
 s.db.close();console.log('BASELINE .59/current deployed route reproduced: same Switch rejected at full limit via QR (409).');
}
{
 const s=setup();await s.ready;const id=add(s,1,fp,'My custom name');
 for(let i=0;i<30;i++){
  const c=await code(s);assert.equal(c.status,200);assert(!JSON.stringify(c.body).includes(fp));
  assert.equal((await poll(s,c)).status,428);assert.equal((await approve(s,c)).status,200);
  const t=await poll(s,c);assert.equal(t.status,200);assert.equal(t.body.device.id,id);
  assert.equal((await poll(s,c)).body.device.id,id);
  assert.equal(s.get('SELECT count(*) n FROM devices').n,1);
  assert.equal(s.get('SELECT name FROM devices WHERE id=?',[id]).name,'My custom name');
 }
 assert(s.invalidated.length>0);
 const password=s.passwordResolve({id:1,max_devices:1},{fingerprint:fp,type:'tv',name:'Nintendo Switch'},'192.0.2.1');
 assert.equal(password.deviceId,id);assert.equal(s.get('SELECT count(*) n FROM devices WHERE user_id=1').n,1);
 // Reusing the same name, same NAT, or another account's key is not identity.
 assert.equal((await approve(s,await code(s,fp2))).status,409);
 const nofp=await s.call('POST','/code',{device_name:'Nintendo Switch',device_type:'console'});
 assert.equal((await approve(s,nofp)).status,409);
 add(s,2,fp2);
 assert.equal((await approve(s,await code(s,fp),{id:2,max_devices:1})).status,409);
 // Removal revokes an already-claimed code; it cannot resurrect the device.
 const c=await code(s);await approve(s,c);await poll(s,c);
 const del=await s.call('DELETE','/mine/:id',{id},account);assert.equal(del.status,200);
 assert.equal((await poll(s,c)).status,403);assert.equal(s.get('SELECT count(*) n FROM devices WHERE user_id=1').n,0);
 s.db.close();
}
{
 const s=setup();await s.ready;
 s.migrate();s.migrate();
 const a=await code(s,fp),b=await code(s,fp2);
 assert.equal((await approve(s,a)).status,200);
 assert.equal((await approve(s,b)).status,409); // reserved at approval, not delayed until token poll
 assert.equal(s.get('SELECT count(*) n FROM devices WHERE user_id=1').n,1);
 assert.equal((await poll(s,a)).status,200);
 // A code bound to another account's row is rejected even at token polling.
 const foreign=add(s,2,fp2);s.run('UPDATE device_pairings SET device_id=? WHERE device_code=?',[foreign,a.body.device_code]);
 assert.equal((await poll(s,a)).status,403);
 s.db.close();
}
{
 const s=setup();await s.ready;
 assert.equal(s.all('PRAGMA table_info(device_pairings)').filter(x=>x.name==='device_fingerprint').length,1);
 const malformed=await code(s,'../invalid');assert.equal(s.get('SELECT device_fingerprint FROM device_pairings WHERE device_code=?',[malformed.body.device_code]).device_fingerprint,null);
 const expired=await code(s);s.run('UPDATE device_pairings SET expires_at=? WHERE device_code=?',['2000-01-01T00:00:00Z',expired.body.device_code]);
 assert.equal((await approve(s,expired)).status,400);assert.equal((await poll(s,expired)).status,400);
 assert.equal(s.get('SELECT count(*) n FROM devices').n,0);
 // Transaction rolls back the reserved device if pairing update fails.
 const c=await code(s);s.db.exec("CREATE TRIGGER test_abort BEFORE UPDATE ON device_pairings BEGIN SELECT RAISE(ABORT,'injected failure'); END;");
 await assert.rejects(()=>approve(s,c));assert.equal(s.get('SELECT count(*) n FROM devices').n,0);
 s.db.close();
}
console.log('Candidate backend: real SQLite reuse at full limit, 30 QR re-logins, reservation, legacy/no-fingerprint, account isolation, revocation, rollback and additive schema passed. Not deployed; Fastify/billing/JWT mocked.');
