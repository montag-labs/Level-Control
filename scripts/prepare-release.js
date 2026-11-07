#!/usr/bin/env node

import { createHash, createSign } from 'crypto';
import { spawnSync } from 'child_process';
import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const buildDir = join(root, '.pio', 'build', 'esp32-c3-devkitm-1');
const version = JSON.parse(readFileSync(join(root, 'version.json'), 'utf8'));
const versionStr = `${Number(version.major) || 0}.${Number(version.minor) || 0}.${Number(version.commit) || 0}`;
const releaseDir = join(root, 'release', `v${versionStr}`);
const privateKeyPath = process.env.RELEASE_SIGNING_PRIVATE_KEY || join(root, 'signing', 'release_private.pem');
const EMBEDDED_SIG_MAGIC = Buffer.from('LCSIGV1!', 'ascii');
const EMBEDDED_SIG_BYTES = 72;

function loadPrivateKeyPem() {
  if (!existsSync(privateKeyPath)) {
    throw new Error(
      `Signatur-Schlüssel fehlt: ${privateKeyPath}\n` +
      'Bitte zuerst ausführen: node scripts/generate-release-signing-keys.js'
    );
  }
  return readFileSync(privateKeyPath, 'utf8');
}

function signBinaryPayload(buffer, privateKeyPem) {
  return Buffer.concat([buffer, createEmbeddedSignatureTrailer(createSignature(buffer, privateKeyPem))]);
}

function createSignature(buffer, privateKeyPem) {
  const signer = createSign('SHA256');
  signer.update(buffer);
  signer.end();

  const signature = signer.sign(privateKeyPem);
  if (signature.length === 0 || signature.length > EMBEDDED_SIG_BYTES) {
    throw new Error(`Signatur-Länge ${signature.length} Byte ist ungültig (max ${EMBEDDED_SIG_BYTES})`);
  }

  return signature;
}

function createEmbeddedSignatureTrailer(signature) {
  const trailer = Buffer.alloc(EMBEDDED_SIG_MAGIC.length + 1 + EMBEDDED_SIG_BYTES, 0);
  EMBEDDED_SIG_MAGIC.copy(trailer, 0);
  trailer.writeUInt8(signature.length, EMBEDDED_SIG_MAGIC.length);
  signature.copy(trailer, EMBEDDED_SIG_MAGIC.length + 1);

  return trailer;
}

// Release-Notes kommen aus CHANGELOG.md (Abschnitt "## <version>"). Commit-Titel wie "v1.5.1" sind als
// Beschreibung unbrauchbar, deshalb gibt es keinen automatischen Fallback: ein Release ohne Eintrag wird abgebrochen.
function getChangelogSection(version) {
  const changelogPath = join(root, 'CHANGELOG.md');
  if (!existsSync(changelogPath)) {
    throw new Error('CHANGELOG.md fehlt. Bitte einen Abschnitt "## ' + version + '" anlegen.');
  }

  const lines = readFileSync(changelogPath, 'utf8').split(/\r?\n/);
  const headingPattern = /^##\s+v?(\d+\.\d+\.\d+)\b/;
  const start = lines.findIndex((line) => headingPattern.exec(line)?.[1] === version);
  if (start === -1) {
    throw new Error(`CHANGELOG.md enthält keinen Abschnitt "## ${version}". Bitte die Änderungen dieser Version beschreiben.`);
  }

  let end = lines.findIndex((line, index) => index > start && /^##\s/.test(line));
  if (end === -1) {
    end = lines.length;
  }

  const section = lines
    .slice(start + 1, end)
    .join('\n')
    .trim();
  if (!section) {
    throw new Error(`Der CHANGELOG.md-Abschnitt "## ${version}" ist leer.`);
  }
  return section;
}


const assets = [
  { source: 'bootloader.bin', target: `level-control-v${versionStr}-bootloader.bin`, kind: 'bootloader' },
  { source: 'partitions.bin', target: `level-control-v${versionStr}-partitions.bin`, kind: 'partitions' }
];

const otaAssets = [
  {
    source: 'firmware.bin',
    target: `level-control-v${versionStr}-image.bin`,
    legacyTarget: `level-control-v${versionStr}-app.bin`,
    signatureTarget: `level-control-v${versionStr}-image.sig`,
    kind: 'app'
  },
  {
    source: 'littlefs.bin',
    target: `level-control-v${versionStr}-filesystem.bin`,
    legacyTarget: `level-control-v${versionStr}-web-ui.bin`,
    signatureTarget: `level-control-v${versionStr}-filesystem.sig`,
    kind: 'webui'
  }
];

mkdirSync(releaseDir, { recursive: true });
const privateKeyPem = loadPrivateKeyPem();

const copiedAssets = assets.map((asset) => {
  const sourcePath = join(buildDir, asset.source);
  const targetPath = join(releaseDir, asset.target);
  if (!existsSync(sourcePath)) {
    throw new Error(`Fehlendes Build-Artefakt: ${asset.source}`);
  }

  copyFileSync(sourcePath, targetPath);
  const buffer = readFileSync(targetPath);

  const sha256 = createHash('sha256').update(buffer).digest('hex');
  return {
    ...asset,
    size: buffer.length,
    sha256,
    targetPath
  };
});

for (const asset of otaAssets) {
  const sourcePath = join(buildDir, asset.source);
  if (!existsSync(sourcePath)) {
    throw new Error(`Fehlendes Build-Artefakt: ${asset.source}`);
  }

  const payload = readFileSync(sourcePath);
  const signature = createSignature(payload, privateKeyPem);
  const payloadPath = join(releaseDir, asset.target);
  const signaturePath = join(releaseDir, asset.signatureTarget);
  const legacyPath = join(releaseDir, asset.legacyTarget);
  writeFileSync(payloadPath, payload);
  writeFileSync(signaturePath, signature);
  writeFileSync(legacyPath, Buffer.concat([payload, createEmbeddedSignatureTrailer(signature)]));

  for (const [target, buffer] of [
    [asset.target, payload],
    [asset.signatureTarget, signature],
    [asset.legacyTarget, readFileSync(legacyPath)]
  ]) {
    copiedAssets.push({
      ...asset,
      target,
      size: buffer.length,
      sha256: createHash('sha256').update(buffer).digest('hex'),
      targetPath: join(releaseDir, target)
    });
  }
}

const sums = copiedAssets
  .map((asset) => `${asset.sha256}  ${asset.target}`)
  .join('\n') + '\n';
writeFileSync(join(releaseDir, 'SHA256SUMS.txt'), sums);

const changelogSection = getChangelogSection(versionStr);
const app = copiedAssets.find((asset) => asset.kind === 'app' && asset.target.endsWith('-image.bin'));
const webui = copiedAssets.find((asset) => asset.kind === 'webui' && asset.target.endsWith('-filesystem.bin'));
const appLegacy = copiedAssets.find((asset) => asset.kind === 'app' && asset.target.endsWith('-app.bin'));
const webuiLegacy = copiedAssets.find((asset) => asset.kind === 'webui' && asset.target.endsWith('-web-ui.bin'));
const appSignature = copiedAssets.find((asset) => asset.kind === 'app' && asset.target.endsWith('-image.sig'));
const webuiSignature = copiedAssets.find((asset) => asset.kind === 'webui' && asset.target.endsWith('-filesystem.sig'));
const bootloader = copiedAssets.find((asset) => asset.kind === 'bootloader');
const partitions = copiedAssets.find((asset) => asset.kind === 'partitions');

const releaseNotes = [
  `## Level-Control v${versionStr}`,
  '',
  changelogSection,
  '',
  '### Update installieren',
  '- **Über die Web-UI (empfohlen):** Admin → Update → Repo-Update. Das Gerät lädt App und Web-UI selbst, prüft die Signatur und startet neu.',
  `- **Manuell:** \`${appLegacy.target}\` und \`${webuiLegacy.target}\` unter Admin → Update lokal hochladen.`,
  `- **Recovery per Kabel (esptool):** \`esptool --chip esp32c3 write_flash 0x0 ${bootloader.target} 0x8000 ${partitions.target} 0x10000 ${app.target} 0x360000 ${webui.target}\``,
  '',
  '### Dateien',
  `- \`${appLegacy.target}\`, \`${webuiLegacy.target}\`: signierte Update-Dateien (Signatur eingebettet, für Web-UI/OTA)`,
  `- \`${app.target}\`, \`${webui.target}\` und die zugehörigen \`.sig\`-Dateien (\`${appSignature.target}\`, \`${webuiSignature.target}\`): unveränderte Images mit getrennter Signatur`,
  `- \`${bootloader.target}\`, \`${partitions.target}\`: nur für Recovery per Kabel`,
  '- `SHA256SUMS.txt`: Prüfsummen aller Dateien'
].join('\n') + '\n';

writeFileSync(join(releaseDir, 'release-notes.txt'), releaseNotes);

process.stdout.write(`Release-Artefakte erstellt: release/v${versionStr}\n`);
