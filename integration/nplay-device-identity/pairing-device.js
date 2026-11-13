// Candidate for Nplay/src/lib/pairing-device.js. Not deployed by the NRO build.
import { all, get, run, tx } from '../db/index.js';

export function installPairingIdentitySchema() {
  tx(() => {
    if (!all('PRAGMA table_info(device_pairings)').some(c => c.name === 'device_fingerprint'))
      run('ALTER TABLE device_pairings ADD COLUMN device_fingerprint TEXT');
  });
}

export function pairingFingerprint(value) {
  return typeof value === 'string' && /^nplay-switch-[a-fA-F0-9]{32}$/.test(value) ? value : null;
}

// Must run inside the caller's transaction, AFTER authenticated user approval.
// An installation fingerprint is not authentication and never selects a user.
export function claimPairingDevice(user, pairing, ip) {
  let existing = null;
  if (pairing.device_id) {
    existing = get('SELECT id FROM devices WHERE id=? AND user_id=? AND approved=1', [pairing.device_id, user.id]);
    if (!existing) return { code: 403, error: 'invalid_device_binding' };
  }
  const fingerprint = pairingFingerprint(pairing.device_fingerprint);
  if (!existing && fingerprint)
    existing = get('SELECT id FROM devices WHERE user_id=? AND fingerprint=? AND approved=1 ORDER BY id LIMIT 1', [user.id, fingerprint]);
  if (existing) {
    run("UPDATE devices SET last_seen_at=datetime('now'), last_ip=? WHERE id=? AND user_id=?", [ip, existing.id, user.id]);
    return { id: existing.id };
  }
  const limit = Number(user.max_devices);
  const count = get('SELECT COUNT(*) n FROM devices WHERE user_id=? AND approved=1', [user.id]).n;
  if (!Number.isSafeInteger(limit) || limit < 1 || count >= limit)
    return { code: 409, error: `Limite de ${Number.isSafeInteger(limit) ? limit : 0} dispositivos atingido. Remova um antes.` };
  const result = run(`INSERT INTO devices(user_id,name,type,fingerprint,approved,last_ip,last_seen_at)
    VALUES(?,?,?,?,1,?,datetime('now'))`, [user.id, pairing.device_name, pairing.device_type, fingerprint, ip]);
  return { id: Number(result.lastInsertRowid) };
}
