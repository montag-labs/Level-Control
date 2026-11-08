#!/usr/bin/env node
// Prueft die Release-Artefakte in release/v<version>/ gegen den aktuellen Build:
// Image unveraendert, Signatur im Trailer gueltig, SHA256SUMS vollstaendig.

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
const trailerMagic = Buffer.from('LCSIGV1!', 'ascii');
const trailerSize = trailerMagic.length + 1 + maxSignatureBytes;
const publicKeyHeader = readFileSync(join(root, 'include', 'ReleaseSigningPublicKey.h'), 'utf8');
const publicKeyPem = publicKeyHeader
  .match(/-----BEGIN PUBLIC KEY-----[\s\S]+?-----END PUBLIC KEY-----/)?.[0]
  ?.replaceAll('\\n', '\n');

assert.ok(publicKeyPem, 'Public-Key konnte nicht aus ReleaseSigningPublicKey.h gelesen werden');

const assets = [
  {
    kind: 'app',
    rawName: 'firmware.bin',
    releaseName: `level-control-v${versionStr}-app.bin`,
    minimumSize: 65536,
    firstByte: 0xe9
  },
  {
    kind: 'webui',
    rawName: 'littlefs.bin',
    releaseName: `level-control-v${versionStr}-web-ui.bin`,
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

// Simuliert den Empfang wie die Firmware: Stuecke unterschiedlicher Groesse, die letzten 81 Byte werden zurueckgehalten.
function simulateReceiver(buffer) {
  const chunkPattern = [137, 4096, 8191, 512, 16384];
  let tail = Buffer.alloc(0);
  let payloadReceived = 0;
  let offset = 0;
  let patternIndex = 0;
  while (offset < buffer.length) {
    const size = Math.min(chunkPattern[patternIndex % chunkPattern.length], buffer.length - offset);
    tail = Buffer.concat([tail, buffer.subarray(offset, offset + size)]);
    offset += size;
    patternIndex += 1;
    if (tail.length > trailerSize) {
      const process = tail.length - trailerSize;
      payloadReceived += process;
      tail = tail.subarray(process);
    }
  }
  assert.equal(tail.length, trailerSize, 'Signatur-Trailer ist nicht vollständig erhalten');
  return { payloadReceived, tail };
}

for (const asset of assets) {
  const rawPath = join(buildDir, asset.rawName);
  const releasePath = join(releaseDir, asset.releaseName);
  assert.ok(existsSync(rawPath), `Build-Artefakt fehlt: ${asset.rawName}`);
  assert.ok(existsSync(releasePath), `Release-Artefakt fehlt: ${asset.releaseName}`);

  const raw = readFileSync(rawPath);
  const release = readFileSync(releasePath);
  assert.ok(raw.length >= asset.minimumSize, `${asset.kind}-Nutzlast ist unplausibel klein`);
  assert.equal(release.length, raw.length + trailerSize, `${asset.kind}-Datei hat nicht die erwartete Größe (Image + Trailer)`);
  assert.deepEqual(release.subarray(0, raw.length), raw, `${asset.kind}-Image entspricht nicht dem aktuellen Build-Artefakt`);
  if (asset.firstByte !== null) {
    assert.equal(release[0], asset.firstByte, `${asset.kind}-Datei ist kein ESP32-App-Image`);
  } else {
    assert.notEqual(release[0], 0xe9, `${asset.kind}-Datei sieht wie ein App-Image aus`);
  }

  const { payloadReceived, tail } = simulateReceiver(release);
  assert.equal(payloadReceived, raw.length, `${asset.kind}: Nutzlast und erwartete Größe weichen ab`);
  assert.deepEqual(tail.subarray(0, trailerMagic.length), trailerMagic, `${asset.kind}: Trailer-Magic stimmt nicht`);
  const signatureLength = tail[trailerMagic.length];
  assert.ok(signatureLength > 0 && signatureLength <= maxSignatureBytes, `${asset.kind}: Signaturlänge ist ungültig`);
  const signature = tail.subarray(trailerMagic.length + 1, trailerMagic.length + 1 + signatureLength);

  const verifier = createVerify('SHA256');
  verifier.update(raw);
  verifier.end();
  assert.equal(verifier.verify(publicKeyPem, signature), true, `${asset.kind}-Signatur ist ungültig`);

  assertChecksum(asset.releaseName, release);
  console.log(`OTA regression OK: ${asset.releaseName} (${raw.length} byte payload)`);
}

for (const name of [`level-control-v${versionStr}-bootloader.bin`, `level-control-v${versionStr}-partitions.bin`]) {
  assert.ok(existsSync(join(releaseDir, name)), `Release-Artefakt fehlt: ${name}`);
  assertChecksum(name, readFileSync(join(releaseDir, name)));
}

// Recovery-Images: unveraendert der Build ohne Trailer; Namen duerfen nicht als Update-Asset erkannt werden.
for (const [name, rawName] of [[`level-control-v${versionStr}-recovery-appimage.bin`, 'firmware.bin'], [`level-control-v${versionStr}-recovery-uiimage.bin`, 'littlefs.bin']]) {
  assert.ok(existsSync(join(releaseDir, name)), `Release-Artefakt fehlt: ${name}`);
  const file = readFileSync(join(releaseDir, name));
  assert.deepEqual(file, readFileSync(join(buildDir, rawName)), `${name} entspricht nicht dem Build-Artefakt`);
  assert.ok(!name.endsWith('-app.bin') && !name.endsWith('-web-ui.bin'), `${name} würde vom Gerät als Update erkannt`);
  assertChecksum(name, file);
}

console.log(`OTA regression erfolgreich für v${versionStr}`);
