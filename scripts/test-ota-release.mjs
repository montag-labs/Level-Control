#!/usr/bin/env node
// Prueft die Release-Artefakte in release/v<version>/ gegen den aktuellen Build:
// Images unveraendert, getrennte Signaturen gueltig, SHA256SUMS vollstaendig.

import assert from 'node:assert/strict';
import { createHash, createVerify } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const version = JSON.parse(readFileSync(join(root, 'version.json'), 'utf8'));
const versionStr = `${Number(version.major) || 0}.${Number(version.minor) || 0}.${Number(version.commit) || 0}`;
const releaseDir = join(root, 'release', `v${versionStr}`);
const buildDir = join(root, '.pio', 'build', 'esp32-c3-devkitm-1');
const maxSignatureBytes = 72;
const publicKeyHeader = readFileSync(join(root, 'include', 'ReleaseSigningPublicKey.h'), 'utf8');
const publicKeyPem = publicKeyHeader
  .match(/-----BEGIN PUBLIC KEY-----[\s\S]+?-----END PUBLIC KEY-----/)?.[0]
  ?.replaceAll('\\n', '\n');

assert.ok(publicKeyPem, 'Public-Key konnte nicht aus ReleaseSigningPublicKey.h gelesen werden');

const assets = [
  {
    kind: 'app',
    rawName: 'firmware.bin',
    releaseName: `level-control-v${versionStr}-image.bin`,
    signatureName: `level-control-v${versionStr}-image.sig`,
    minimumSize: 65536,
    firstByte: 0xe9
  },
  {
    kind: 'webui',
    rawName: 'littlefs.bin',
    releaseName: `level-control-v${versionStr}-filesystem.bin`,
    signatureName: `level-control-v${versionStr}-filesystem.sig`,
    minimumSize: 4096,
    firstByte: null
  }
];

const sums = new Map(
  readFileSync(join(releaseDir, 'SHA256SUMS.txt'), 'utf8')
    .split(/\r?\n/)
    .filter(Boolean)
    .map((line) => {
      const [hash, name] = line.split(/\s+/);
      return [name, hash];
    })
);

function assertChecksum(name, buffer) {
  assert.equal(
    sums.get(name),
    createHash('sha256').update(buffer).digest('hex'),
    `SHA256SUMS.txt stimmt für ${name} nicht`
  );
}

for (const asset of assets) {
  const rawPath = join(buildDir, asset.rawName);
  const releasePath = join(releaseDir, asset.releaseName);
  const signaturePath = join(releaseDir, asset.signatureName);
  assert.ok(existsSync(rawPath), `Build-Artefakt fehlt: ${asset.rawName}`);
  assert.ok(existsSync(releasePath), `Release-Artefakt fehlt: ${asset.releaseName}`);
  assert.ok(existsSync(signaturePath), `Signatur fehlt: ${asset.signatureName}`);

  const raw = readFileSync(rawPath);
  const release = readFileSync(releasePath);
  const signature = readFileSync(signaturePath);
  assert.ok(raw.length >= asset.minimumSize, `${asset.kind}-Nutzlast ist unplausibel klein`);
  assert.deepEqual(release, raw, `${asset.kind}-Release-Image entspricht nicht dem aktuellen Build-Artefakt`);
  assert.ok(signature.length > 0 && signature.length <= maxSignatureBytes, `${asset.kind}-Signatur ist ungültig`);
  if (asset.firstByte !== null) {
    assert.equal(release[0], asset.firstByte, `${asset.kind}-Datei ist kein ESP32-App-Image`);
  } else {
    assert.notEqual(release[0], 0xe9, `${asset.kind}-Datei sieht wie ein App-Image aus`);
  }

  const verifier = createVerify('SHA256');
  verifier.update(raw);
  verifier.end();
  assert.equal(verifier.verify(publicKeyPem, signature), true, `${asset.kind}-Signatur ist ungültig`);

  assertChecksum(asset.releaseName, release);
  assertChecksum(asset.signatureName, signature);
  console.log(`OTA regression OK: ${asset.releaseName} (${raw.length} byte payload)`);
}

for (const name of [`level-control-v${versionStr}-bootloader.bin`, `level-control-v${versionStr}-partitions.bin`]) {
  assert.ok(existsSync(join(releaseDir, name)), `Release-Artefakt fehlt: ${name}`);
  assertChecksum(name, readFileSync(join(releaseDir, name)));
}

console.log(`OTA regression erfolgreich für v${versionStr}`);
