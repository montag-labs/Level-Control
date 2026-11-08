#!/usr/bin/env node
// Erzeugt release/v<version>/: signierte Update-Dateien (App und Web-UI, Signatur als Trailer), Recovery-Dateien,
// SHA256SUMS.txt und release-notes.txt (aus CHANGELOG.md). Voraussetzung: frischer Build + buildfs.

import { createHash, createSign } from 'crypto';
import { copyFileSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const buildDir = join(root, '.pio', 'build', 'esp32-c3-devkitm-1');
const version = JSON.parse(readFileSync(join(root, 'version.json'), 'utf8'));
const versionStr = `${Number(version.major) || 0}.${Number(version.minor) || 0}.${Number(version.commit) || 0}`;
if (version.label) {
  throw new Error(`version.json enthält das Dev-Label "${version.label}" - Releases müssen ohne Label gebaut werden.`);
}
const releaseDir = join(root, 'release', `v${versionStr}`);
const privateKeyPath = process.env.RELEASE_SIGNING_PRIVATE_KEY || join(root, 'signing', 'release_private.pem');
const MAX_SIGNATURE_BYTES = 72;
const TRAILER_MAGIC = Buffer.from('LCSIGV1!', 'ascii');

function loadPrivateKeyPem() {
  if (!existsSync(privateKeyPath)) {
    throw new Error(
      `Signatur-Schlüssel fehlt: ${privateKeyPath}\n` +
      'Bitte zuerst ausführen: node scripts/generate-release-signing-keys.js'
    );
  }
  return readFileSync(privateKeyPath, 'utf8');
}

function createSignature(buffer, privateKeyPem) {
  const signer = createSign('SHA256');
  signer.update(buffer);
  signer.end();

  const signature = signer.sign(privateKeyPem);
  if (signature.length === 0 || signature.length > MAX_SIGNATURE_BYTES) {
    throw new Error(`Signatur-Länge ${signature.length} Byte ist ungültig (max ${MAX_SIGNATURE_BYTES})`);
  }

  return signature;
}

// Trailer (81 Byte): Magic "LCSIGV1!" (8), Laenge der Signatur (1), Signatur (72, mit Nullen aufgefuellt).
// Die Firmware haelt beim Schreiben die letzten 81 Byte zurueck und prueft damit die Signatur ueber das Image.
function createTrailer(signature) {
  const trailer = Buffer.alloc(TRAILER_MAGIC.length + 1 + MAX_SIGNATURE_BYTES, 0);
  TRAILER_MAGIC.copy(trailer, 0);
  trailer.writeUInt8(signature.length, TRAILER_MAGIC.length);
  signature.copy(trailer, TRAILER_MAGIC.length + 1);
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

const changelogSection = getChangelogSection(versionStr);

// Nur fuer Recovery per Kabel (esptool): Bootloader, Partitionen und die Images OHNE Signatur-Trailer.
// Die signierte Web-UI-Datei waere 81 Byte groesser als ihre Partition und liesse sich nicht flashen.
// Die Namen enden bewusst nicht auf "-app.bin" / "-web-ui.bin", damit das Geraet sie nicht fuer Updates haelt.
const recoveryAssets = [
  { source: 'bootloader.bin', target: `level-control-v${versionStr}-bootloader.bin` },
  { source: 'partitions.bin', target: `level-control-v${versionStr}-partitions.bin` },
  { source: 'firmware.bin', target: `level-control-v${versionStr}-recovery-appimage.bin` },
  { source: 'littlefs.bin', target: `level-control-v${versionStr}-recovery-uiimage.bin` }
];

const otaAssets = [
  { source: 'firmware.bin', target: `level-control-v${versionStr}-app.bin` },
  { source: 'littlefs.bin', target: `level-control-v${versionStr}-web-ui.bin` }
];

// Frisch anlegen, damit keine Dateien frueherer Laeufe (z. B. aus dem alten Trailer-Format) zurueckbleiben.
rmSync(releaseDir, { recursive: true, force: true });
mkdirSync(releaseDir, { recursive: true });
const privateKeyPem = loadPrivateKeyPem();

function readBuildArtifact(name) {
  const sourcePath = join(buildDir, name);
  if (!existsSync(sourcePath)) {
    throw new Error(`Fehlendes Build-Artefakt: ${name}`);
  }
  return readFileSync(sourcePath);
}

const files = [];  // { name, buffer }

for (const asset of recoveryAssets) {
  copyFileSync(join(buildDir, asset.source), join(releaseDir, asset.target));
  files.push({ name: asset.target, buffer: readBuildArtifact(asset.source) });
}

for (const asset of otaAssets) {
  const payload = readBuildArtifact(asset.source);
  const signed = Buffer.concat([payload, createTrailer(createSignature(payload, privateKeyPem))]);
  writeFileSync(join(releaseDir, asset.target), signed);
  files.push({ name: asset.target, buffer: signed });
}

const sums = files
  .map((file) => `${createHash('sha256').update(file.buffer).digest('hex')}  ${file.name}`)
  .join('\n') + '\n';
writeFileSync(join(releaseDir, 'SHA256SUMS.txt'), sums);

const [app, webui] = otaAssets;
const [bootloader, partitions, recoveryApp, recoveryUi] = recoveryAssets;

const releaseNotes = [
  `## Level-Control v${versionStr}`,
  '',
  changelogSection,
  '',
  '### Update installieren',
  '- **Über die Web-UI (empfohlen):** Admin → Update → Repo-Update. Das Gerät lädt App und Web-UI selbst, prüft die Signatur und startet neu.',
  `- **Lokal:** Admin → Update → „Updates hochladen“: \`${app.target}\` und/oder \`${webui.target}\` auswählen.`,
  `- **Recovery per Kabel (esptool):** \`esptool --chip esp32c3 write_flash 0x0 ${bootloader.target} 0x8000 ${partitions.target} 0x10000 ${recoveryApp.target} 0x360000 ${recoveryUi.target}\``,
  '',
  '### Dateien',
  `- \`${app.target}\`, \`${webui.target}\`: App und Web-UI (signiert; die Signatur steckt in der Datei)`,
  `- \`${bootloader.target}\`, \`${partitions.target}\`, \`${recoveryApp.target}\`, \`${recoveryUi.target}\`: nur für Recovery per Kabel (unsigniert, kein Update-Weg)`,
  '- `SHA256SUMS.txt`: Prüfsummen aller Dateien'
].join('\n') + '\n';

writeFileSync(join(releaseDir, 'release-notes.txt'), releaseNotes);

process.stdout.write(`Release-Artefakte erstellt: release/v${versionStr}\n`);
