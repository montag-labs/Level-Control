#!/usr/bin/env node

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const read = (path) => readFileSync(join(root, path), 'utf8');
const app = read('ui/src/App.svelte');
const dashboard = read('ui/src/Dashboard.svelte');
const backup = read('ui/src/Backup.svelte');
const settingModules = ['Sensor', 'Wifi', 'Mqtt', 'Push'].map((name) => read(`ui/src/config/${name}Settings.svelte`));
const admin = read('ui/src/Admin.svelte');
// Die Web-Schicht ist auf mehrere Dateien verteilt; die Verträge gelten für ihre Gesamtheit.
const firmware = [
  'src/web/WebServerDashboard.cpp',
  'src/web/UpdateService.cpp',
  'src/web/FlashSession.cpp',
  'lib/LevelCore/src/SignatureTrailer.cpp',
  'src/web/AdminSession.cpp',
  'src/web/WebSupport.cpp'
].map(read).join('\n');
const configStore = read('src/config/ConfigStore.cpp');

function test(name, assertion) {
  assertion();
  console.log(`Phase-6 contract OK: ${name}`);
}

test('sensor valid=false does not refresh live timestamp', () => {
  assert.match(app, /data\.sensorValid\s*=\s*msg\.valid\s*!==\s*false/);
  assert.match(app, /if \(data\.sensorValid\) \{/);
  assert.match(app, /lastSensorUpdate\s*=\s*Date\.now\(\)/);
});

test('sensor error state is visible', () => {
  assert.match(dashboard, /sensorState === 'error'/);
  assert.match(dashboard, /sensorError/);
  assert.match(dashboard, /sensorStateLabel/);
});

test('ConfigStore save failures are surfaced', () => {
  assert.match(firmware, /sensor_config_save_failed/);
  assert.match(firmware, /mqtt_config_save_failed/);
  assert.match(firmware, /wifi_config_save_failed/);
  assert.match(configStore, /bool ConfigStore::save\(const Config& \w+\)/);
});

test('backup roundtrip keeps sensor type and masks secrets by default', () => {
  assert.match(firmware, /cfgDoc\["sensorType"\]\s*=\s*config\.sensorType/);
  assert.match(firmware, /config\.sensorType\s*=\s*jsonString\(cfg,\s*"sensorType"/);
  assert.match(firmware, /cfgDoc\["tofRoiSize"\]\s*=\s*config\.tofRoiSize/);
  assert.match(firmware, /config\.tofRoiSize\s*=\s*cfg\["tofRoiSize"\]/);
  assert.match(firmware, /includeSecrets/);
  assert.match(firmware, /includeSecrets \? config\.wifi\.password : kPasswordMask/);
  assert.match(firmware, /includeSecrets \? config\.mqtt\.password : kPasswordMask/);
  assert.match(firmware, /includeSecrets \? config\.push\.authPassword : kPasswordMask/);
  assert.match(backup, /includeBackupSecrets/);
});

test('history delete checks HTTP success', () => {
  assert.match(dashboard, /if \(!response\.ok\)/);
  assert.match(dashboard, /history\s*=\s*\[\]/);
});

test('dirty navigation guard is wired', () => {
  for (const source of settingModules) {
    assert.match(source, /onDirtyStateChange/);
  }
  assert.match(admin, /activeModuleDirty/);
  assert.match(admin, /unsavedConfirm/);
  assert.match(app, /adminHasUnsavedChanges/);
});

test('admin authentication protects sensitive routes', () => {
  assert.match(firmware, /\/api\/auth\/setup/);
  assert.match(firmware, /\/api\/auth\/login/);
  assert.match(firmware, /admin_auth_required/);
  for (const route of ['/api/export', '/api/import', '/api/factory-reset', '/api/restart', '/api/update/repo']) {
    assert.ok(firmware.includes(route), `route missing: ${route}`);
  }
});

test('request size limits are enforced before allocation', () => {
  assert.match(firmware, /kMaxConfigBodyBytes/);
  assert.match(firmware, /kMaxWifiBodyBytes/);
  assert.match(firmware, /kMaxMqttBodyBytes/);
  assert.match(firmware, /kMaxImportBodyBytes/);
  assert.match(firmware, /kMaxUpdateRequestBytes/);
  assert.match(firmware, /payload_too_large/);
});

test('body handlers never accept follow-up chunks of a rejected request', () => {
  // ESPAsyncWebServer ruft den Body-Handler auch nach einem abgelehnten ersten Chunk weiter auf.
  // Ein Puffer darf deshalb ausschliesslich bei index == 0 (nach Auth- und Groessenpruefung) angelegt werden.
  assert.match(firmware, /const char\s*\*\s*collectBody\(/);
  assert.doesNotMatch(firmware, /Defensive fallback/);
  assert.doesNotMatch(firmware, /_tempObject\s*=\s*new\s/);
  assert.match(firmware, /if \(index == 0 && !requireAdmin\(request\)\) return;\s*const char \*body = (?:web::)?collectBody/);
});

test('only signed images are accepted', () => {
  // Die Signatur steckt als Trailer in der Datei; ein Update ohne gültigen Trailer wird verworfen.
  assert.match(firmware, /'L', 'C', 'S', 'I', 'G', 'V', '1', '!'/);  // Trailer-Magic
  assert.match(firmware, /mbedtls_pk_verify/);
  assert.match(firmware, /Signatur-Trailer fehlt oder ist ungültig/);
  assert.doesNotMatch(firmware, /api\/update\/signature/);
});

test('configuration reads and login are protected', () => {
  for (const route of ['/api/wifi', '/api/mqtt', '/api/push']) {
    assert.ok(
      firmware.includes(`server_.on("${route}", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;`),
      `GET ${route} muss Admin verlangen`
    );
  }
  assert.match(firmware, /admin_login_locked/);
  assert.match(firmware, /recordLoginFailure\(\)/);
});

test('OTA HIL supports token or password authentication', () => {
  const hil = read('scripts/test-ota-hardware.mjs');
  assert.match(hil, /OTA_TEST_ADMIN_TOKEN/);
  assert.match(hil, /OTA_TEST_ADMIN_PASSWORD/);
  assert.match(hil, /\/api\/auth\/login/);
});

console.log('Phase-6 contract regression erfolgreich');
