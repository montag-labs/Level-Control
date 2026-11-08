<script>
  import { confirmAction, showNotice } from './dialogStore.js';
  import { adminFetch } from './adminAuth.js';
  import { createTranslator } from './i18n.js';

  export let loadConfig;
  export let lang = 'de';

  let importFile = null;
  let importLoading = false;
  let importResult = null;
  let factoryResetLoading = false;
  let includeBackupSecrets = false;
  $: t = createTranslator(lang);

  async function doExport() {
    try {
      if (includeBackupSecrets) {
        const confirmed = await confirmAction({
          title: t('backup.secretsTitle'),
          message: t('backup.secretsMessage'),
          confirmLabel: t('backup.secretsConfirm'),
          cancelLabel: t('backup.cancel'),
          tone: 'danger'
        });
        if (!confirmed) return;
      }

      const query = includeBackupSecrets ? '?includeSecrets=1' : '';
      const response = await adminFetch(`/api/export${query}`, { cache: 'no-store' });
      if (!response.ok) throw new Error(t('backup.exportFailed'));

      const blob = await response.blob();
      const url = URL.createObjectURL(blob);
      const link = document.createElement('a');
      link.href = url;
      link.download = 'level-control-backup.json';
      link.click();
      URL.revokeObjectURL(url);
    } catch (error) {
      showNotice('error', error?.message || t('backup.exportFailed'));
    }
  }

  async function doImport() {
    if (!importFile) return;
    importLoading = true;
    importResult = null;
    try {
      const body = await importFile.arrayBuffer();
      const response = await adminFetch('/api/import', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body
      });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok || payload.status !== 'ok') {
        throw new Error(payload.error || t('backup.importFailed'));
      }

      const parts = [];
      if (payload.configRestored) parts.push(t('backup.partConfig'));
      if (payload.historyRestored) parts.push(t('backup.partHistory'));
      importResult = { ok: true, msg: t('backup.restoredWith', { parts: parts.length ? parts.join(t('backup.partsJoin')) : t('backup.nothing') }) };
      if (payload.configRestored) await loadConfig();
    } catch (error) {
      importResult = { ok: false, msg: error?.message || t('backup.importFailed') };
    } finally {
      importLoading = false;
    }
  }

  async function doFactoryReset() {
    if (factoryResetLoading) return;
    const confirmed = await confirmAction({
      title: t('backup.factoryConfirmTitle'),
      message: t('backup.factoryConfirmMessage'),
      confirmLabel: t('backup.factoryConfirmButton'),
      cancelLabel: t('backup.cancel'),
      tone: 'danger'
    });
    if (!confirmed) return;

    factoryResetLoading = true;
    try {
      const response = await adminFetch('/api/factory-reset', { method: 'POST' });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok || payload.status !== 'ok') {
        throw new Error(payload.error || t('backup.factoryFailed'));
      }
      showNotice('success', t('backup.factoryStarted'));
    } catch (error) {
      showNotice('error', error?.message || t('backup.factoryFailed'));
    } finally {
      factoryResetLoading = false;
    }
  }
</script>

<div class="backup-module">
  <h2>{t('backup.title')}</h2>
  <h3>{t('backup.export')}</h3>
  <p>{t('backup.exportHint')}</p>
  <label class="checkbox-row checkbox-row--warning"><input type="checkbox" bind:checked={includeBackupSecrets} /> {t('backup.includeSecrets')}</label>
  <button class="primary" on:click={doExport}>{t('backup.download')}</button>

  <h3>{t('backup.import')}</h3>
  <p>{t('backup.importHint')}</p>
  <input type="file" accept=".json" on:change={(event) => { importFile = event.currentTarget.files?.[0] || null; importResult = null; }} />
  <button class="primary" disabled={!importFile || importLoading} on:click={doImport}>
    {importLoading ? t('backup.importing') : t('backup.import')}
  </button>
  {#if importResult}<p class:error={!importResult.ok} class:success={importResult.ok}>{importResult.msg}</p>{/if}

  <h3>{t('backup.factory')}</h3>
  <p>{t('backup.factoryHint')}</p>
  <button class="danger" disabled={factoryResetLoading} on:click={doFactoryReset}>
    {factoryResetLoading ? t('backup.factoryRunning') : t('backup.factoryButton')}
  </button>
</div>

<style>
  .backup-module { display: grid; gap: 12px; padding: 20px; border: 1px solid var(--surface-border); border-radius: 12px; background: var(--card-grad); color: var(--text-main); }
  h2, h3, p { margin: 0; }
  h3 { margin-top: 12px; color: var(--text-main); }
  p { color: var(--text-muted); line-height: 1.5; }
  input[type='file'] { color: var(--text-muted); }
  .checkbox-row { display: flex; gap: 8px; align-items: center; color: var(--text-muted); font-weight: 700; }
  button { min-height: 40px; width: fit-content; padding: 0 14px; border: 1px solid var(--surface-border); border-radius: 8px; background: var(--button-active-bg); color: var(--button-active-text); font-weight: 800; cursor: pointer; }
  button:disabled { cursor: default; opacity: 0.55; }
  button.danger { background: rgba(180, 45, 45, 0.85); }
  .success { color: #86efac; }
  .error { color: #fca5a5; }
</style>
