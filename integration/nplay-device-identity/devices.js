// Candidate for Nplay/src/routes/devices.js. Requires pairing-device.js alongside.
import { get, run, all, tx } from '../db/index.js';
import { randomBytes } from '../lib/crypto.js';
import { config } from '../config.js';
import { withBillingLimits } from '../lib/billing.js';
import { hit } from '../lib/rate-limit.js';
import { invalidateAuthCache } from '../lib/auth-cache.js';
import { pairingUrl, pairingMatrix } from '../../public/tv/pairing.js';
import { installPairingIdentitySchema, pairingFingerprint, claimPairingDevice } from '../lib/pairing-device.js';

function genUserCode() { return String(Math.floor(100000 + Math.random() * 900000)); }

export default async function deviceRoutes(app) {
  // Additive, idempotent, before serving requests. Schema failure fails startup,
  // never silently drops identity support. Existing devices/rows are untouched.
  installPairingIdentitySchema();
  app.post('/code', async (req, reply) => {
    const guard = hit(`device-code:${req.ip}`, { max: 12, windowMs: 10 * 60 * 1000 });
    if (!guard.allowed) return reply.code(429).header('retry-after', String(guard.retryAfterSec))
      .send({ error: 'Muitos códigos solicitados. Aguarde alguns minutos.', retry_after: guard.retryAfterSec });
    const deviceName = String(req.body?.device_name || 'Smart TV').trim().slice(0, 80) || 'Smart TV';
    const deviceType = String(req.body?.device_type || 'tv').trim().slice(0, 24) || 'tv';
    const fingerprint = pairingFingerprint(req.body?.device_fingerprint);
    run('DELETE FROM device_pairings WHERE expires_at < ?', [new Date().toISOString()]);
    let userCode;
    do { userCode = genUserCode(); } while (get('SELECT 1 FROM device_pairings WHERE user_code=? AND status=\'pending\'', [userCode]));
    const deviceCode = randomBytes(24).toString('hex');
    const expires = new Date(Date.now() + 10 * 60 * 1000).toISOString();
    run(`INSERT INTO device_pairings(user_code,device_code,device_name,device_type,device_fingerprint,ip,expires_at)
      VALUES(?,?,?,?,?,?,?)`, [userCode, deviceCode, deviceName, deviceType, fingerprint, req.ip, expires]);
    const response = {
      user_code: userCode, device_code: deviceCode,
      verification_uri: `${config.publicUrl}/#/pair`,
      verification_uri_complete: `${config.publicUrl}/#/pair?code=${userCode}`,
      interval: 5, expires_in: 600,
    };
    response.qr = pairingMatrix(pairingUrl(response));
    return reply.header('cache-control', 'no-store').send(response);
  });
  app.post('/token', async (req, reply) => {
    const deviceCode = String(req.body?.device_code || '');
    if (!/^[a-f0-9]{48}$/.test(deviceCode)) return reply.code(400).send({ error: 'invalid_device_code' });
    const guard = hit(`device-token:${req.ip}`, { max: 150, windowMs: 10 * 60 * 1000 });
    if (!guard.allowed) return reply.code(429).header('retry-after', '5').send({ error: 'slow_down', interval: 5 });
    const p = get('SELECT * FROM device_pairings WHERE device_code=?', [deviceCode]);
    if (!p) return reply.code(400).send({ error: 'invalid_device_code' });
    if (new Date(p.expires_at) < new Date()) return reply.code(400).send({ error: 'expired_token' });
    if (p.status === 'pending') return reply.code(428).send({ status: 'authorization_pending' });
    if (!['approved', 'claimed'].includes(p.status)) return reply.code(403).send({ error: 'access_denied' });
    // Approval reserves/reuses a concrete device. Deleting that device revokes
    // the pairing too; a repeated token poll must not recreate it silently.
    if (!p.device_id) return reply.code(403).send({ error: 'access_denied' });
    const raw = get(`SELECT id,username,role,max_devices,max_sessions,device_limit_override,session_limit_override
      FROM users WHERE id=?`, [p.user_id]);
    if (!raw) return reply.code(403).send({ error: 'access_denied' });
    const user = withBillingLimits(raw);
    const claim = tx(() => {
      const result = claimPairingDevice(user, p, req.ip);
      if (result.id) run("UPDATE device_pairings SET device_id=?,status='claimed' WHERE id=?", [result.id, p.id]);
      return result;
    });
    if (!claim.id) return reply.code(claim.code).send({ error: claim.error });
    invalidateAuthCache(user.id);
    const token = app.jwt.sign({ sub: user.id, role: user.role, deviceId: claim.id }, { expiresIn: '180d' });
    return { token, user, device: { id: claim.id, name: p.device_name, type: p.device_type } };
  });
  app.post('/approve', { preHandler: [app.authenticate] }, async (req, reply) => {
    const userCode = String(req.body?.user_code || '').replace(/\D/g, '').slice(0, 6);
    if (!/^\d{6}$/.test(userCode)) return reply.code(400).send({ error: 'Código inválido' });
    const claim = tx(() => {
      const p = get("SELECT * FROM device_pairings WHERE user_code=? AND status='pending'", [userCode]);
      if (!p) return { code: 404, error: 'Código inválido ou expirado' };
      if (new Date(p.expires_at) < new Date()) return { code: 400, error: 'Código expirado' };
      const result = claimPairingDevice(req.account, p, req.ip);
      if (result.id) run("UPDATE device_pairings SET status='approved',user_id=?,device_id=? WHERE id=?", [req.account.id, result.id, p.id]);
      return { ...result, name: p.device_name };
    });
    if (!claim.id) return reply.code(claim.code).send({ error: claim.error });
    invalidateAuthCache(req.account.id);
    return { ok: true, device_name: claim.name };
  });
  app.get('/mine', { preHandler: [app.authenticate] }, async req => ({
    devices: all(`SELECT id,name,type,approved,last_ip,last_seen_at,created_at FROM devices
      WHERE user_id=? ORDER BY created_at DESC`, [req.account.id]),
  }));
  app.delete('/mine/:id', { preHandler: [app.authenticate] }, async (req, reply) => {
    const r = run('DELETE FROM devices WHERE id=? AND user_id=?', [Number(req.params.id), req.account.id]);
    if (!r.changes) return reply.code(404).send({ error: 'Dispositivo não encontrado' });
    invalidateAuthCache(req.account.id);
    return { ok: true };
  });
}
