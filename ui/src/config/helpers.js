import { adminFetch } from '../adminAuth.js';

export const PASSWORD_MASK = '*****';

export function isMaskedPassword(value) {
  return value === '***' || value === PASSWORD_MASK;
}

// Grenzen wie in der Firmware (ConfigLimits in lib/LevelCore/src/Config.h)
export const LIMITS = {
  minSampleSeconds: 5,
  minTankHeightCm: 10,
  maxTankHeightCm: 500,
  maxOffsetCm: 200
};

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Startet einen E-Mail-Auftrag im Gerät (Test-Mail oder SMTP-Diagnose) und wartet auf das Ergebnis.
// Das Gerät führt SMTP in einem eigenen Task aus: POST startet (202), GET /api/push/job liefert den Stand.
export async function runPushJob(startUrl, { timeoutMs = 90000 } = {}) {
  const start = await adminFetch(startUrl, { method: 'POST' });
  if (start.status === 409) {
    const error = new Error('push_job_running');
    error.code = 'busy';
    throw error;
  }
  if (!start.ok) {
    const payload = await start.json().catch(() => ({}));
    throw new Error(payload.error || `HTTP ${start.status}`);
  }

  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    await sleep(1000);
    const response = await adminFetch('/api/push/job', { cache: 'no-store' });
    const job = await response.json().catch(() => null);
    if (job?.state === 'done') {
      return job;
    }
  }
  throw new Error('timeout');
}
