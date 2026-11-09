#!/usr/bin/env node
// Browser-Smoke-Test der gebauten Web-UI (data/) gegen ein simuliertes Gerät – ohne Hardware.
//
//   node scripts/ui-smoke.mjs [--dir=data] [--out=<Ordner für Screenshots>] [--dump=<Datei.json>] [--lang=en]
//
// Startet einen lokalen Server (statische Dateien + Mock der /api-Routen + gefälschter WebSocket), öffnet die UI in
// einem Chromium-Browser (Edge/Chrome, oder Pfad per BROWSER=...), meldet sich als Admin an, besucht alle Admin-Module,
// speichert Einstellungen und prüft die dabei gesendeten Requests. Jeder Seitenfehler oder Konsolenfehler lässt den
// Test fehlschlagen. Mit --dump werden die sichtbaren Formularfelder jedes Moduls als JSON festgehalten, so lassen
// sich zwei UI-Stände (vorher/nachher) vergleichen.

import assert from 'node:assert/strict';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const args = Object.fromEntries(process.argv.slice(2).map((arg) => {
  const [key, value] = arg.replace(/^--/, '').split('=');
  return [key, value ?? true];
}));
const dataDir = resolve(root, args.dir || 'data');
const outDir = args.out ? resolve(root, args.out) : '';
const language = args.lang || 'de';

const require = createRequire(join(root, 'ui', 'package.json'));
const puppeteer = require('puppeteer-core');

function findBrowser() {
  const candidates = [
    process.env.BROWSER,
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    '/usr/bin/google-chrome',
    '/usr/bin/google-chrome-stable',
    '/usr/bin/chromium',
    '/usr/bin/chromium-browser'
  ].filter(Boolean);
  const found = candidates.find((path) => existsSync(path));
  if (!found) {
    throw new Error('Kein Chromium-Browser gefunden. Pfad über BROWSER=... angeben.');
  }
  return found;
}

// ------------------------------------------------------------------ simuliertes Gerät

const state = {
  loggedIn: false,
  calls: [],
  config: { behaelterhoehe: 95, offset: 0, sampleIntervalSeconds: 300, sensorType: 'rcwl1670' },
  wifi: {
    ssid: 'HeimNetz', password: '*****', hasPassword: true, deviceName: 'Stand',
    ntpServerPrimary: 'pool.ntp.org', ntpServerSecondary: 'time.cloudflare.com', dnsHostname: 'stand',
    localUrl: 'http://stand/', useStaticIp: false, staticIp: { ip: '', gateway: '', subnet: '', dns: '' }
  },
  mqtt: { server: 'mqtt.local', port: 1883, user: 'ha', password: '*****', hasPassword: true, discovery: true, device_id: 'level_control_AABBCCDDEEFF' },
  push: {
    enabled: true, smtpServer: 'smtp.example.org', smtpPort: 465, useSsl: true, startTls: false, encryption: 'ssl',
    smtpSkipCertVerify: false, authUser: 'mailer', authPassword: '*****', hasAuthPassword: true,
    senderName: 'Level-Control', senderEmail: 'stand@example.org', recipientEmail: 'ich@example.org',
    triggerPercent: 20, sendHour: 8, sendMinute: 0, reminderCycle: 'week', reminderWeekday: 1,
    subjectTemplate: 'Salz fast leer', bodyTemplate: 'Füllstand {level_percent}%'
  }
};

const updateStatus = {
  inProgress: false, success: false, rebootPending: false, source: '', target: '', phase: 'idle', message: '',
  installedVersion: '1.5.1', availableVersion: '', pendingVersion: '', manifestError: '', received: 0, total: 0,
  uploadActive: false, activityAgeMs: 0, uploadStallTimeoutMs: 300000, canReset: false,
  configFallbackApplied: false, configFallbackMessage: '', appMaxSize: 1572864, firmwareMaxSize: 1572864,
  webuiMaxSize: 655360, releaseTagBaseUrl: 'https://github.com/example/Level-Control/releases/tag/v',
  lastResetReason: 'poweron'
};

const manifest = {
  fetching: false, version: '1.5.2', releaseUrl: 'https://github.com/example/Level-Control/releases/tag/v1.5.2',
  assets: {
    app: { name: 'level-control-v1.5.2-image.bin', url: 'https://example.invalid/image.bin', size: 1300000 },
    webui: { name: 'level-control-v1.5.2-filesystem.bin', url: 'https://example.invalid/fs.bin', size: 655360 }
  }
};

function json(res, status, body) {
  res.writeHead(status, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
  res.end(JSON.stringify(body));
}

async function readBody(req) {
  const chunks = [];
  for await (const chunk of req) chunks.push(chunk);
  return Buffer.concat(chunks).toString('utf8');
}

async function handleApi(req, res, path) {
  const method = req.method;
  const body = method === 'POST' ? await readBody(req) : '';
  if (method !== 'GET') {
    state.calls.push({ method, path, body: body ? (() => { try { return JSON.parse(body); } catch { return body; } })() : null });
  }

  const routes = {
    'GET /api/auth/status': () => json(res, 200, { configured: true, authenticated: state.loggedIn, sessionTtlMs: 28800000 }),
    'POST /api/auth/login': () => { state.loggedIn = true; json(res, 200, { status: 'ok', token: 'test-token' }); },
    'POST /api/auth/logout': () => { state.loggedIn = false; json(res, 200, { status: 'ok' }); },
    'GET /api/config': () => json(res, 200, state.config),
    'POST /api/config': () => json(res, 200, { status: 'ok' }),
    'GET /api/wifi': () => json(res, 200, state.wifi),
    'POST /api/wifi': () => json(res, 200, { status: 'ok' }),
    'POST /api/wifi/scan': () => json(res, 200, { networks: [{ ssid: 'HeimNetz', rssi: -50 }, { ssid: 'Nachbar', rssi: -80 }] }),
    'GET /api/mqtt': () => json(res, 200, state.mqtt),
    'POST /api/mqtt': () => json(res, 200, { status: 'ok' }),
    'GET /api/mqtt/status': () => json(res, 200, { state: 'connected', server: 'mqtt.local', port: 1883, deviceId: state.mqtt.device_id }),
    'POST /api/mqtt/reconnect': () => json(res, 200, { status: 'ok' }),
    'GET /api/push': () => json(res, 200, state.push),
    'POST /api/push': () => json(res, 200, { status: 'ok' }),
    'POST /api/push/test': () => json(res, 202, { status: 'started' }),
    'POST /api/push/smtp-check': () => json(res, 202, { status: 'started' }),
    'GET /api/push/job': () => json(res, 200, { state: 'done', kind: 'diagnostic', success: true, error: '', steps: [{ name: 'DNS', ok: true, detail: 'ok' }] }),
    'GET /api/history': () => json(res, 200, [{ ts: 1767225600, v: 62.5 }, { ts: 1767247200, v: 61.0 }]),
    'GET /api/laser-version': () => json(res, 200, { version: 'VL53L1X' }),
    'GET /api/update/status': () => json(res, 200, updateStatus),
    'GET /api/update/manifest': () => json(res, 200, manifest),
    'GET /api/export': () => json(res, 200, { exportVersion: 1, config: {}, history: [] }),
    'POST /api/import': () => json(res, 200, { status: 'ok', configRestored: true, historyRestored: true })
  };
  const handler = routes[`${method} ${path}`];
  if (handler) {
    handler();
  } else {
    json(res, 404, { error: `mock: ${method} ${path}` });
  }
}

const mime = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.svg': 'image/svg+xml', '.json': 'application/json' };

const server = createServer(async (req, res) => {
  const url = new URL(req.url, 'http://localhost');
  if (url.pathname.startsWith('/api/')) {
    await handleApi(req, res, url.pathname);
    return;
  }
  const file = join(dataDir, url.pathname === '/' ? 'index.html' : url.pathname);
  if (file.startsWith(dataDir) && existsSync(file)) {
    res.writeHead(200, { 'Content-Type': mime[extname(file)] || 'application/octet-stream' });
    res.end(readFileSync(file));
  } else {
    res.writeHead(404);
    res.end('not found');
  }
});

// ------------------------------------------------------------------ Browser-Szenario

const fakeWebSocket = `
  class FakeWebSocket {
    constructor(url) {
      this.url = url;
      this.readyState = 0;
      setTimeout(() => {
        this.readyState = 1;
        if (this.onopen) this.onopen({});
        const send = (payload) => this.onmessage && this.onmessage({ data: JSON.stringify(payload) });
        send({ type: 'sensor', valid: true, ping_us: 2900, rohdistanz: 0.5, salzstandCm: 45, salzstandPercent: 47.4 });
        send({ type: 'wifi', signal: -60, ip: '192.168.1.50', ssid: 'HeimNetz', bssid: 'AA:BB:CC:DD:EE:FF' });
        send({ type: 'uptime', uptime: 3600 });
        send({ type: 'mqtt', state: 'connected' });
      }, 50);
    }
    send() {}
    close() { this.readyState = 3; if (this.onclose) this.onclose({}); }
    addEventListener() {}
    removeEventListener() {}
  }
  window.WebSocket = FakeWebSocket;
  localStorage.setItem('stand-language', ${JSON.stringify(language)});
`;

const problems = [];
const dump = {};

async function main() {
  await new Promise((resolveListen) => server.listen(0, '127.0.0.1', resolveListen));
  const baseUrl = `http://127.0.0.1:${server.address().port}`;
  if (outDir) mkdirSync(outDir, { recursive: true });

  // Eigenes Profil, damit weder ein laufender Browser noch Reste früherer Läufe stören.
  const userDataDir = mkdtempSync(join(tmpdir(), 'ui-smoke-'));
  const browser = await puppeteer.launch({ executablePath: findBrowser(), headless: true, userDataDir, args: ['--no-sandbox'] });
  try {
    const page = await browser.newPage();
    page.setDefaultTimeout(10000);
    await page.setViewport({ width: 1280, height: 1000 });
    await page.evaluateOnNewDocument(fakeWebSocket);
    // Der Wechsel zwischen Modulen mit ungespeicherten Änderungen fragt per window.confirm nach.
    dump.dialogs = [];
    page.on('dialog', async (dialog) => {
      dump.dialogs.push(dialog.message());
      await dialog.accept();
    });
    page.on('pageerror', (error) => problems.push(`pageerror: ${error.message}`));
    page.on('console', (message) => {
      if (message.type() === 'error' && !/404 \(Not Found\)|Failed to load resource/.test(message.text())) {
        problems.push(`console.error: ${message.text()}`);
      }
    });

    const step = (name) => { if (args.verbose) console.log('step:', name); };
    const shot = async (name) => { if (outDir) await page.screenshot({ path: join(outDir, `${name}.png`) }); };
    const pause = (ms = 250) => new Promise((r) => setTimeout(r, ms));
    // Feldinhalt ersetzen (Strg+A, dann tippen), damit Svelte die input-Ereignisse bekommt
    const setValue = async (handle, text) => {
      await handle.click();
      await page.keyboard.down('Control');
      await page.keyboard.press('KeyA');
      await page.keyboard.up('Control');
      await page.keyboard.type(text);
    };

    // Sichtbare Formularfelder + Überschriften eines Moduls erfassen
    const snapshot = async (name) => {
      dump[`${name}Text`] = await page.evaluate(() => (document.querySelector('.admin-module-content') || document.body).innerText);
      dump[name] = await page.evaluate(() => {
        const content = document.querySelector('.admin-module-content') || document.body;
        const fields = [...content.querySelectorAll('input, select, textarea')].map((el) => {
          const label = el.closest('label, .field-row')?.textContent?.replace(/\s+/g, ' ').trim().slice(0, 60) || '';
          const value = el.type === 'checkbox' || el.type === 'radio' ? String(el.checked) : el.value;
          return [el.tagName.toLowerCase() + (el.type ? `:${el.type}` : ''), label, value];
        });
        const headings = [...content.querySelectorAll('h2, h3')].map((el) => el.textContent.trim());
        const buttons = [...content.querySelectorAll('button')].map((el) => el.textContent.replace(/\s+/g, ' ').trim());
        return { headings, buttons, fields };
      });
    };

    step('goto');
    await page.goto(baseUrl, { waitUntil: 'networkidle0' });
    await pause(500);
    await shot('01-dashboard');
    dump.dashboardText = await page.evaluate(() => document.body.innerText.replace(/\s+/g, ' ').slice(0, 600));

    // Admin öffnen + anmelden
    const tabs = await page.$$('.tabs .tab');
    assert.ok(tabs.length >= 2, 'Navigation nicht gefunden');
    await tabs[1].click();
    await page.waitForSelector('.admin-login input[type=password]', { timeout: 5000 });
    await shot('02-login');
    await page.type('.admin-login input[type=password]', 'geheim123');
    await page.click('.admin-login button.primary');
    await page.waitForSelector('.admin-module-nav button', { timeout: 5000 });
    await pause(400);

    const moduleCount = (await page.$$('.admin-module-nav button')).length;
    assert.equal(moduleCount, 8, 'Es sollten 8 Admin-Module angezeigt werden');
    const moduleNames = ['sensor', 'wifi', 'mqtt', 'push', 'backup', 'password', 'update', 'system'];
    const openModule = async (index) => {
      const buttons = await page.$$('.admin-module-nav button');
      await buttons[index].click();
      await pause(500);
    };

    for (let index = 0; index < moduleNames.length; index += 1) {
      step(`module ${moduleNames[index]}`);
      await openModule(index);
      await shot(`03-${moduleNames[index]}`);
      await snapshot(moduleNames[index]);
    }

    step('sensor edit');
    // --- Sensor: Höhe ändern und speichern
    await openModule(0);
    const numberInputs = await page.$$('.admin-module-content input[type=number]');
    assert.ok(numberInputs.length >= 3, 'Sensor-Felder fehlen');
    await setValue(numberInputs[1], '120');
    await pause(150);
    dump.saveBarEnabledAfterEdit = await page.evaluate(() => !document.querySelector('.save-bar-save')?.disabled);
    await shot('04-sensor-dirty');
    await page.click('.admin-module-content button.primary');
    await pause(500);
    const sensorCall = state.calls.filter((call) => call.path === '/api/config').pop();
    assert.ok(sensorCall, 'POST /api/config wurde nicht gesendet');
    assert.equal(sensorCall.body.behaelterhoehe, 120);
    assert.equal(sensorCall.body.sampleIntervalSeconds, 300);
    assert.equal(sensorCall.body.sensorType, 'rcwl1670');

    step('sensor invalid');
    // --- Sensor: ungültige Höhe wird abgelehnt (kein weiterer POST)
    const before = state.calls.length;
    await setValue(numberInputs[1], '5');
    await page.click('.admin-module-content button.primary');
    await pause(300);
    assert.equal(state.calls.length, before, 'Ungültige Höhe darf nicht gesendet werden');

    step('wifi');
    // --- WLAN: Gerätename ändern und speichern
    await openModule(1);
    step('wifi opened');
    const wifiTexts = await page.$$('.admin-module-content input:not([type=radio]):not([type=checkbox]):not([type=password])');
    step(`wifi fields ${wifiTexts.length}`);
    assert.ok(wifiTexts.length >= 4, 'WLAN-Felder fehlen');
    await setValue(wifiTexts[1], 'Keller');
    step('wifi typed');
    await page.click('.admin-module-content button.primary');
    step('wifi clicked');
    await pause(400);
    const wifiCall = state.calls.filter((call) => call.path === '/api/wifi').pop();
    assert.ok(wifiCall, 'POST /api/wifi wurde nicht gesendet');
    assert.equal(wifiCall.body.deviceName, 'Keller');
    assert.equal(wifiCall.body.ssid, 'HeimNetz');
    assert.equal(wifiCall.body.useStaticIp, false);

    step('mqtt');
    // --- MQTT: Server ändern und speichern
    await openModule(2);
    const mqttTexts = await page.$$('.admin-module-content input:not([type=checkbox]):not([type=password]):not([type=number])');
    await setValue(mqttTexts[0], 'broker.lan');
    await page.click('.admin-module-content button.primary');
    await pause(400);
    const mqttCall = state.calls.filter((call) => call.path === '/api/mqtt').pop();
    assert.ok(mqttCall, 'POST /api/mqtt wurde nicht gesendet');
    assert.equal(mqttCall.body.server, 'broker.lan');
    assert.equal(mqttCall.body.discovery, true);

    step('push');
    // --- Push: Schwellwert ändern, speichern, Test-Mail
    await openModule(3);
    const pushNumbers = await page.$$('.admin-module-content input[type=number]');
    await setValue(pushNumbers[1], '35');
    await page.click('.admin-module-content button.primary');
    await pause(400);
    const pushCall = state.calls.filter((call) => call.path === '/api/push').pop();
    assert.ok(pushCall, 'POST /api/push wurde nicht gesendet');
    assert.equal(pushCall.body.triggerPercent, 35);
    assert.equal(pushCall.body.reminderCycle, 'week');
    assert.equal(pushCall.body.encryption, 'ssl');
    assert.equal(pushCall.body.smtpPort, 465);

    // --- Push: SMTP-Diagnose und Test-Mail laufen als Auftrag im Gerät (POST startet, GET /api/push/job liefert das Ergebnis)
    step('push jobs');
    const jobButtons = await page.$$('.admin-module-content .secondary-sm');
    assert.ok(jobButtons.length >= 2, 'Buttons für SMTP-Diagnose und Test-Mail fehlen');
    await jobButtons[0].click();
    await pause(2600);
    assert.ok(state.calls.some((call) => call.path === '/api/push/smtp-check'), 'SMTP-Diagnose wurde nicht gestartet');
    const diagnosticText = await page.evaluate(() => document.querySelector('.diag-result')?.innerText || '');
    assert.match(diagnosticText, /DNS/, 'Ergebnis der SMTP-Diagnose fehlt');
    await jobButtons[1].click();
    await pause(2600);
    assert.ok(state.calls.some((call) => call.path === '/api/push/test'), 'Test-Mail wurde nicht gestartet');

    step('update');
    // --- Update-Modul: Manifest-Anzeige
    await openModule(6);
    const updateText = await page.evaluate(() => document.querySelector('.admin-module-content').innerText);
    assert.match(updateText, /1\.5\.2/, 'Verfügbare Version fehlt im Update-Modul');

    step('language');
    // --- Sprache wechseln
    await page.select('.language-pick select', language === 'en' ? 'de' : 'en');
    await pause(400);
    await shot('05-language-switched');
    dump.updateAfterLanguageSwitch = await page.evaluate(() => document.querySelector('.admin-module-content').innerText.replace(/\s+/g, ' ').slice(0, 400));
  } finally {
    await browser.close();
    server.close();
  }

  // Englische UI: Reste deutscher Texte melden (Nutzdaten wie Mail-Vorlagen und Provider-Hinweise ausgenommen).
  if (language === 'en') {
    const german = /[äöüÄÖÜß]|\b(Speichern|Einstellungen|Gerät|Bitte|nicht|Verbindung|Konfiguration|Abbrechen|Fehler|Datei|wurde|wird)\b/;
    const ignore = /Füllstand \{level|Zertifikat|Hetzner|Benutzerdefiniert|Stand Control|HeimNetz|Salz fast leer/;
    for (const [key, text] of Object.entries(dump)) {
      if (!key.endsWith('Text') || typeof text !== 'string') continue;
      for (const line of text.split('\n')) {
        if (german.test(line) && !ignore.test(line)) problems.push(`Deutscher Text in englischer UI (${key}): ${line.trim().slice(0, 90)}`);
      }
    }
  }

  if (args.strict) {
    assert.equal(dump.saveBarEnabledAfterEdit, true, 'Die Speichern-Leiste muss nach einer Änderung aktiv werden');
  }

  if (args.dump) {
    writeFileSync(resolve(root, args.dump), JSON.stringify(dump, null, 2));
  }

  if (problems.length > 0) {
    console.error(`UI-Smoke-Test: ${problems.length} Problem(e)`);
    problems.slice(0, 20).forEach((problem) => console.error(`  - ${problem}`));
    process.exit(1);
  }
  console.log(`UI-Smoke-Test erfolgreich (${dataDir}, Sprache ${language})`);
}

main().catch((error) => {
  console.error(error);
  server.close();
  process.exit(1);
});
