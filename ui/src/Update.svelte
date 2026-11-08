<script>
  import { onMount } from 'svelte';
  import { confirmAction, showNotice } from './dialogStore.js';
  import { adminFetch, getAdminToken } from './adminAuth.js';
  import { translate as t, tr } from './i18n.js';

  export let currentVersion = '0.0.0';

  // Tatsächlich laufende Version – kommt vom Gerät via /api/update/status.
  // Fällt auf den compile-time Prop zurück bis der erste Fetch abgeschlossen ist.
  let liveInstalledVersion = '';

  let statusCard = null;
  let appFileInput = null;
  let webUiFileInput = null;

  let manifest = null;
  let manifestError = '';
  let status = {
    inProgress: false,
    success: false,
    rebootPending: false,
    source: '',
    target: '',
    phase: 'idle',
    message: '',
    availableVersion: '',
    pendingVersion: '',
    received: 0,
    total: 0,
    uploadActive: false,
    activityAgeMs: 0,
    canReset: false,
    configFallbackApplied: false,
    configFallbackMessage: '',
    appMaxSize: 0,
    firmwareMaxSize: 0,
    webuiMaxSize: 0,
    lastResetReason: '',
    lastResetReasonCode: 0
  };

  let appFile = null;
  let appSigFile = null;
  let webUiFile = null;
  let webUiSigFile = null;
  let localUpload = {
    active: false,
    target: '',
    received: 0,
    total: 0
  };
  let otaReloadScheduled = false;
  let rebootFallbackTriggered = false;

  // Dev-Staende wie "1.5.A" (Buchstabe statt PATCH) gelten als aelter als jedes numerische 1.5.x.
  function versionPart(part) {
    return /^[A-Za-z]$/.test(part) ? -1 : (Number(part) || 0);
  }

  function compareVersions(left, right) {
    const leftParts = left.split('.').map(versionPart);
    const rightParts = right.split('.').map(versionPart);
    for (let index = 0; index < 3; index += 1) {
      const delta = (leftParts[index] || 0) - (rightParts[index] || 0);
      if (delta !== 0) return delta;
    }
    return 0;
  }

  const targetName = (target) => tr(target === 'app' ? 'update.app' : 'update.webui');

  function humanSize(bytes) {
    if (!bytes) return tr('update.unknownSize');
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
  }

  function progressValue(received, total) {
    if (!total || total <= 0) return 0;
    return Math.min(100, Math.round((received / total) * 100));
  }

  function isRepoProgress(target) {
    return status.inProgress && status.source === 'repo' && status.target === target;
  }

  function isUploadProgress(target) {
    return localUpload.active && localUpload.target === target;
  }

  function formatProgressText(received, total) {
    if (!total) {
      return `${humanSize(received)} übertragen`;
    }
    return `${humanSize(received)} / ${humanSize(total)}`;
  }

  function isBusy() {
    return status.inProgress || localUpload.active;
  }

  function delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }

  function isFirmwareLikeTarget(target) {
    return target === 'app' || target === 'full';
  }

  function uploadFileWithProgress(target, formData, options = {}) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      const deferReboot = options.deferReboot ? '?deferReboot=1' : '';
      xhr.open('POST', `/api/update/upload/${target}${deferReboot}`);
      if (options.adminToken) {
        xhr.setRequestHeader('X-Admin-Token', options.adminToken);
      }
      xhr.responseType = 'text';
      xhr.timeout = 0;

      xhr.upload.onprogress = (event) => {
        if (!event.lengthComputable) {
          return;
        }

        localUpload = {
          active: true,
          target,
          received: event.loaded,
          total: event.total
        };
      };

      xhr.onerror = () => {
        localUpload = { active: false, target: '', received: 0, total: 0 };
        reject(Object.assign(new Error(tr('update.uploadNetworkError')), { code: 'network' }));
      };

      xhr.onabort = () => {
        localUpload = { active: false, target: '', received: 0, total: 0 };
        reject(new Error(tr('update.uploadAborted')));
      };

      xhr.ontimeout = () => {
        localUpload = { active: false, target: '', received: 0, total: 0 };
        reject(Object.assign(new Error(tr('update.uploadTimeout')), { code: 'timeout' }));
      };

      xhr.onload = () => {
        const payload = (() => {
          try {
            return JSON.parse(xhr.responseText || '{}');
          } catch (_) {
            return {};
          }
        })();

        localUpload = {
          active: false,
          target: '',
          received: payload.status === 'ok' ? localUpload.total : 0,
          total: payload.status === 'ok' ? localUpload.total : 0
        };

        if (xhr.status >= 200 && xhr.status < 300) {
          resolve(payload);
          return;
        }

        reject(new Error(payload.error || tr('update.uploadFailed')));
      };

      xhr.send(formData);
    });
  }

  async function loadManifest() {
    try {
      const response = await fetch('/api/update/manifest');
      const payload = await response.json();
      if (!response.ok) {
        manifest = null;
        manifestError = payload.error || tr('update.manifestFailed');
        return;
      }
      manifest = payload;
      manifestError = '';
    } catch (_) {
      manifest = null;
      manifestError = tr('update.manifestFailed');
    }
  }

  async function loadStatus() {
    try {
      const response = await fetch('/api/update/status', { cache: 'no-store' });
      if (response.ok) {
        status = await response.json();
        if (status.installedVersion) {
          liveInstalledVersion = status.installedVersion;
        }

        // After a successful OTA the device should reboot. Poll until device is reachable and reload.
        if (!otaReloadScheduled && status.success && !status.inProgress && (status.rebootPending || isFirmwareLikeTarget(status.target))) {
          otaReloadScheduled = true;
          setTimeout(async () => {
            let attempts = 0;
            while (attempts < 24) {
              try {
                const r = await fetch('/api/update/status', { cache: 'no-store' });
                if (r.ok) {
                  window.location.reload();
                  return;
                }
              } catch (_) { /* Gerät startet noch neu */ }
              await new Promise(res => setTimeout(res, 500));
              attempts++;
            }
            window.location.reload(); // Fallback nach ~12 s
          }, 3000);
        }

        // Safety net: if reboot is still pending for too long, request a restart explicitly once.
        if (
          !rebootFallbackTriggered
          && status.success
          && status.rebootPending
          && !status.inProgress
          && isFirmwareLikeTarget(status.target)
          && (status.activityAgeMs || 0) > 15000
        ) {
          rebootFallbackTriggered = true;
          adminFetch('/api/restart', { method: 'POST' }).catch(() => {});
        }

        return status;
      }
    } catch (_) {
      // Status-Polling ist optional.
    }

    return null;
  }

  async function recoverAfterUploadTransportError(target, previousVersion) {
    for (let attempt = 0; attempt < 12; attempt += 1) {
      await delay(500);
      const nextStatus = await loadStatus();
      if (!nextStatus) {
        continue;
      }

      if (nextStatus.phase === 'failed') {
        return { ok: false, message: nextStatus.message || tr('update.uploadFailed') };
      }

      if (nextStatus.success && nextStatus.rebootPending) {
        return {
          ok: true,
          message: nextStatus.message || tr('update.accepted', { target: targetName(target) })
        };
      }

      if (
        isFirmwareLikeTarget(target)
        && previousVersion
        && nextStatus.installedVersion
        && compareVersions(nextStatus.installedVersion, previousVersion) > 0
      ) {
        return {
          ok: true,
          message: tr('update.newFirmware', { version: nextStatus.installedVersion })
        };
      }

      if (!nextStatus.inProgress && !nextStatus.uploadActive && nextStatus.phase === 'done') {
        return {
          ok: true,
          message: nextStatus.message || tr('update.acceptedNoReboot', { target: targetName(target) })
        };
      }
    }

    if (status.canReset) {
      return {
        ok: false,
        message: tr('update.uploadInterrupted')
      };
    }

    return {
      ok: false,
      message: status.lastResetReason
        ? tr('update.uploadNetworkErrorWithReset', { reason: status.lastResetReason })
        : tr('update.uploadNetworkError')
    };
  }

  async function resetDeviceUpdateState() {
    const response = await adminFetch('/api/update/reset', { method: 'POST' });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      showNotice('error', payload.error || tr('update.resetFailed'));
      return;
    }

    await loadStatus();
    showNotice('success', payload.message || tr('update.resetDone'));
  }

  async function startRepoUpdate() {
    if (isBusy() || !manifest) {
      return;
    }

    // Automatisch erkennen, welche Updates verfügbar sind
    const hasAppUpdate = manifest.assets?.app || manifest.assets?.firmware;
    const hasWebUiUpdate = manifest.assets?.webui;

    let target = 'app'; // default
    let updateLabel = tr('update.appGenitive');

    if (hasAppUpdate && hasWebUiUpdate) {
      target = 'full';
      updateLabel = tr('update.appAndWebui');
    } else if (hasWebUiUpdate && !hasAppUpdate) {
      target = 'webui';
      updateLabel = tr('update.webuiGenitive');
    }

    const confirmed = await confirmAction({
      title: tr('update.repoConfirmTitle'),
      message: tr('update.repoConfirmMessage', { label: updateLabel }),
      confirmLabel: tr('update.repoConfirmButton'),
      cancelLabel: tr('dialog.cancel')
    });

    if (!confirmed) {
      return;
    }

    // Zur Status-Card scrollen
    if (statusCard) {
      statusCard.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }

    const response = await adminFetch('/api/update/repo', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ target })
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      showNotice('error', payload.error || tr('update.repoStartFailed'));
      return;
    }

    showNotice('success', payload.message || tr('update.repoStarted'));
    await loadStatus();
  }

  function getRepoUpdateStatus(manifestData, installedVersion) {
    if (!manifestData) {
      return { hasUpdate: false, label: tr('update.noManifest') };
    }

    const hasAppUpdate = manifestData.assets?.app || manifestData.assets?.firmware;
    const hasWebUiUpdate = manifestData.assets?.webui;
    const appNewer = compareVersions(manifestData.version, installedVersion) > 0;

    if (!appNewer) {
      return { hasUpdate: false, label: tr('update.upToDate') };
    }

    const updates = [];
    if (hasAppUpdate) updates.push(tr('update.app'));
    if (hasWebUiUpdate) updates.push(tr('update.webui'));

    return {
      hasUpdate: updates.length > 0,
      label: tr('update.available', { parts: updates.join(' + ') })
    };
  }

  function validateLocalFile(target, file) {
    if (!file) {
      return tr('update.selectBinFirst');
    }

    const lowerName = file.name.toLowerCase();
    if (!lowerName.endsWith('.bin')) {
      return tr('update.onlyBin');
    }

    if (lowerName.includes('bootloader') || lowerName.includes('partition')) {
      return tr('update.noBootloader');
    }

    const maxAppSize = status.appMaxSize || status.firmwareMaxSize;
    const appFitsPartition = !maxAppSize || file.size <= maxAppSize;
    const webUiFitsPartition = !status.webuiMaxSize || file.size <= status.webuiMaxSize;

    if (target === 'app') {
      if (lowerName.includes('littlefs') || lowerName.includes('web-ui')) {
        return tr('update.looksLikeWebui');
      }
      if (!appFitsPartition) {
        return tr('update.tooBigApp');
      }
      if (file.size < 65536) {
        return tr('update.tooSmallApp');
      }
    } else {
      if (lowerName.includes('firmware') || lowerName.includes('app')) {
        return tr('update.looksLikeApp');
      }
      if (!webUiFitsPartition) {
        return tr('update.tooBigWebui');
      }
      if (file.size < 4096) {
        return tr('update.tooSmallWebui');
      }
    }

    return '';
  }

  // Die .sig-Datei muss vor dem Image beim Gerät liegen; es prüft damit am Ende die Signatur.
  async function uploadSignature(target, signatureFile) {
    const response = await adminFetch(`/api/update/signature/${target}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: signatureFile
    });
    if (!response.ok) {
      const payload = await response.json().catch(() => ({}));
      throw new Error(payload.error || tr('update.sigUploadFailed'));
    }
  }

  async function uploadLocalStep(target, fileToUpload, signatureFile, options = {}) {
    const formData = new FormData();
    formData.append('file', fileToUpload);
    const previousVersion = displayedVersion;
    const label = targetName(target);

    localUpload = { active: true, target, received: 0, total: fileToUpload.size };

    let payload;
    try {
      await uploadSignature(target, signatureFile);
      options.adminToken = await getAdminToken();
      payload = await uploadFileWithProgress(target, formData, options);
    } catch (errorReason) {
      const isTransportFailure = errorReason?.code === 'network' || errorReason?.code === 'timeout';
      if (isTransportFailure && !options.deferReboot) {
        const recovery = await recoverAfterUploadTransportError(target, previousVersion);
        if (!recovery.ok) {
          throw new Error(recovery.message);
        }
        return { status: 'ok', message: recovery.message };
      }

      await loadStatus();
      throw new Error(status.message || errorReason.message || tr('update.uploadFailedLabel', { target: label }));
    }

    return payload;
  }

  async function startLocalUpdate() {
    if (isBusy()) {
      return;
    }

    const plan = [];
    if (webUiFile || webUiSigFile) plan.push({ target: 'webui', file: webUiFile, signature: webUiSigFile });
    if (appFile || appSigFile) plan.push({ target: 'app', file: appFile, signature: appSigFile });

    if (plan.length === 0) {
      showNotice('error', tr('update.noFilesSelected'));
      return;
    }

    for (const step of plan) {
      if (step.file && !step.signature) {
        showNotice('error', tr('update.sigFileMissing', { target: targetName(step.target) }));
        return;
      }
      if (!step.file) {
        showNotice('error', tr('update.binFileMissing', { target: targetName(step.target) }));
        return;
      }
      const error = validateLocalFile(step.target, step.file);
      if (error) {
        showNotice('error', error);
        return;
      }
    }

    const scope = plan.length === 2
      ? tr('update.appAndWebui')
      : targetName(plan[0].target);
    const confirmed = await confirmAction({
      title: tr('update.localConfirmTitle'),
      message: tr('update.localConfirmMessage', { scope }),
      confirmLabel: tr('update.repoConfirmButton'),
      cancelLabel: tr('dialog.cancel')
    });

    if (!confirmed) {
      return;
    }

    if (statusCard) {
      statusCard.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }

    try {
      for (let index = 0; index < plan.length; index += 1) {
        const step = plan[index];
        const isLast = index === plan.length - 1;
        await uploadLocalStep(step.target, step.file, step.signature, { deferReboot: !isLast });
        await loadStatus();
      }

      const endLabel = plan.length === 2 ? tr('update.appAndWebui') : targetName(plan[0].target);
      showNotice('success', tr('update.localSuccess', { scope: endLabel }));
      appFile = null;
      appSigFile = null;
      webUiFile = null;
      webUiSigFile = null;
    } catch (error) {
      showNotice('error', error?.message || tr('update.localFailed'));
    }
  }

  function triggerFileSelect(target) {
    if (target === 'app') {
      appFileInput?.click();
    } else {
      webUiFileInput?.click();
    }
  }

  function handleFileSelected(target) {
    return (event) => {
      // Eine Auswahl darf .bin und .sig gleichzeitig enthalten (Strg/Umschalt).
      const files = Array.from(event.currentTarget.files || []);
      const binFile = files.find((entry) => entry.name.toLowerCase().endsWith('.bin')) || null;
      const sigFile = files.find((entry) => entry.name.toLowerCase().endsWith('.sig')) || null;
      if (!binFile && !sigFile) {
        return;
      }

      // Speichern für spätere Verwendung
      if (target === 'app') {
        if (binFile) appFile = binFile;
        if (sigFile) appSigFile = sigFile;
      } else {
        if (binFile) webUiFile = binFile;
        if (sigFile) webUiSigFile = sigFile;
      }
      showNotice('success', tr('update.selected', { target: targetName(target), names: files.map((entry) => entry.name).join(', ') }));

      // Zur Status-Card scrollen
      if (statusCard) {
        statusCard.scrollIntoView({ behavior: 'smooth', block: 'start' });
      }

      event.currentTarget.value = '';
    };
  }

  $: progressPercent = progressValue(status.received, status.total);
  $: repoProgress = status.inProgress && status.source === 'repo' && (status.target === 'app' || status.target === 'full' || status.target === 'webui')
    ? progressValue(status.received, status.total)
    : 0;
  $: uploadAppProgress = localUpload.active && localUpload.target === 'app' ? progressValue(localUpload.received, localUpload.total) : 0;
  $: uploadWebUiProgress = localUpload.active && localUpload.target === 'webui' ? progressValue(localUpload.received, localUpload.total) : 0;
  $: displayedVersion = liveInstalledVersion || currentVersion;
  $: repoStatus = getRepoUpdateStatus(manifest, displayedVersion);
  $: currentReleaseUrl = status.releaseTagBaseUrl
    ? `${status.releaseTagBaseUrl}${displayedVersion}`
    : `https://github.com/Back-code/Level-Control/releases/tag/v${displayedVersion}`;
  $: anyBusy = status.inProgress || localUpload.active;
  $: localStartDisabled = anyBusy || !((appFile && appSigFile) || (webUiFile && webUiSigFile));
  $: localSelectionLabel = [appFile ? `${$t('update.app')}: ${appFile.name}${appSigFile ? $t('update.withSig') : $t('update.sigMissing')}` : '', webUiFile ? `${$t('update.webui')}: ${webUiFile.name}${webUiSigFile ? $t('update.withSig') : $t('update.sigMissing')}` : '']
    .filter(Boolean)
    .join(' | ');
  $: showResetAction = status.canReset && !localUpload.active;
  $: pendingVersionLabel = status.pendingVersion && isFirmwareLikeTarget(status.target) ? status.pendingVersion : '';

  onMount(() => {
    loadManifest();
    loadStatus();
    const interval = setInterval(() => {
      if (localUpload.active) {
        return;
      }
      loadStatus();
    }, 1000);
    return () => clearInterval(interval);
  });
</script>

<section class="update-grid">
  <article class="status-card hero" bind:this={statusCard}>
    {#if manifestError}
      <div class="manifest-badge warning">{manifestError}</div>
    {:else if manifest}
      <div class="manifest-badge success">{$t('update.manifestLoaded')}</div>
    {/if}

    <div>
      <span class="eyebrow">{$t('update.eyebrow')}</span>
      <h2>{$t('update.center')}</h2>
      <div class="version-row">
        <div class="version-item installed">
          <span class="version-label">{$t('update.installedVersion')}</span>
          <a href={currentReleaseUrl} target="_blank" rel="noopener noreferrer" class="version-link">v{displayedVersion}</a>
        </div>
        {#if manifest}
          <span class="version-separator" aria-hidden="true"></span>
          <div class="version-item available">
            <span class="version-label">{$t('update.availableVersion')}</span>
            <a href={manifest.releaseUrl} target="_blank" rel="noopener noreferrer" class="version-link">v{manifest.version}</a>
          </div>
        {/if}
      </div>
    </div>

    {#if manifest}
      <div class="manifest-box">
        <div>
          <span>{$t('update.app')}</span>
          <strong>{manifest.assets?.app?.name || manifest.assets?.firmware?.name || $t('update.notAvailable')}</strong>
          <small>{humanSize(manifest.assets?.app?.size || manifest.assets?.firmware?.size)}</small>
        </div>
        <div>
          <span>{$t('update.webui')}</span>
          <strong>{manifest.assets?.webui?.name || $t('update.notAvailable')}</strong>
          <small>{humanSize(manifest.assets?.webui?.size)}</small>
        </div>
      </div>
    {/if}

    <div class="status-box" class:busy={status.inProgress}>
      <div class="status-head">
        <span>{$t('update.status')}</span>
        <strong>{$t('update.phases.' + (status.phase || 'idle'), status.phase || 'idle')}</strong>
      </div>
      <p>{status.message || $t('update.noUpdateActive')}</p>
      {#if pendingVersionLabel}
        <small>{$t('update.targetVersion', { version: pendingVersionLabel })}</small>
      {/if}
      {#if status.lastResetReason}
        <small>{$t('update.lastReset', { reason: status.lastResetReason })}</small>
      {/if}
      {#if status.configFallbackApplied}
        <small style="color:#f59e0b;display:block;">
          {$t('update.configCorrected')}
          {#if status.configFallbackMessage}
            ({status.configFallbackMessage})
          {/if}
        </small>
      {/if}
      <div class="progress-track">
        <div class="progress-fill" style={`width:${progressPercent}%`}></div>
      </div>
      <small>{$t('update.bytes', { received: status.received, total: status.total || 0 })}</small>
      {#if showResetAction}
        <button class="secondary-button" on:click={resetDeviceUpdateState}>
          {$t('update.resetState')}
        </button>
      {/if}
    </div>
  </article>

  <article class="action-card repo-card">
    <span class="eyebrow">{$t('update.stepRepo')}</span>
    {#if repoStatus.hasUpdate}
      <div class="status-badge repo-status-badge success">✓ {repoStatus.label}</div>
    {:else}
      <div class="status-badge repo-status-badge info">⊘ {repoStatus.label}</div>
    {/if}
    <h3>{$t('update.repoTitle')}</h3>
    <p>{$t('update.repoHint')}</p>
    <button on:click={() => startRepoUpdate()} disabled={!repoStatus.hasUpdate || anyBusy}>
      {$t('update.repoStart')}
    </button>
    {#if isRepoProgress('app') || isRepoProgress('full') || isRepoProgress('webui')}
      <div class="inline-progress">
        <div class="inline-progress-head">
          <span>{$t('update.repoRunning')}</span>
          <strong>{repoProgress}%</strong>
        </div>
        <div class="mini-track"><div class="mini-fill" style={`width:${repoProgress}%`}></div></div>
        <small>{$t('update.phases.' + status.phase, status.phase)}: {formatProgressText(status.received, status.total)}</small>
      </div>
    {/if}
    {#if manifest?.releaseUrl}
      <a class="release-link" href={manifest.releaseUrl} target="_blank" rel="noopener noreferrer">{$t('update.openRelease')}</a>
    {/if}
  </article>

  <article class="action-card upload-card">
    <span class="eyebrow">{$t('update.stepLocal')}</span>
    <h3>{$t('update.localTitle')}</h3>
    <p>{$t('update.localHint')}</p>

    {#if localSelectionLabel}
      <div class="status-badge local-selection-badge success">✓ {$t('update.selection', { label: localSelectionLabel })}</div>
    {:else}
      <div class="status-badge local-selection-badge info">⊘ {$t('update.noSelection')}</div>
    {/if}
    
    <!-- Hidden file inputs -->
    <input 
      type="file" 
      accept=".bin,.sig"
      multiple
      bind:this={appFileInput}
      style="display:none"
      on:change={handleFileSelected('app')}
    />
    <input 
      type="file" 
      accept=".bin,.sig"
      multiple
      bind:this={webUiFileInput}
      style="display:none"
      on:change={handleFileSelected('webui')}
    />
    
    <!-- Action buttons -->
    <div class="button-group">
      <button on:click={() => triggerFileSelect('app')} disabled={anyBusy}>
        {$t('update.chooseApp')}
      </button>
      <button on:click={() => triggerFileSelect('webui')} disabled={anyBusy}>
        {$t('update.chooseWebui')}
      </button>
      <button on:click={startLocalUpdate} disabled={localStartDisabled}>
        {$t('update.localStart')}
      </button>
    </div>
    
    <!-- Upload progress indicators -->
    {#if isUploadProgress('app')}
      <div class="inline-progress">
        <div class="inline-progress-head">
          <span>{$t('update.uploadingApp')}</span>
          <strong>{uploadAppProgress}%</strong>
        </div>
        <div class="mini-track"><div class="mini-fill" style={`width:${uploadAppProgress}%`}></div></div>
        <small>{formatProgressText(localUpload.received, localUpload.total)}</small>
      </div>
    {/if}
    
    {#if isUploadProgress('webui')}
      <div class="inline-progress">
        <div class="inline-progress-head">
          <span>{$t('update.uploadingWebui')}</span>
          <strong>{uploadWebUiProgress}%</strong>
        </div>
        <div class="mini-track"><div class="mini-fill" style={`width:${uploadWebUiProgress}%`}></div></div>
        <small>{formatProgressText(localUpload.received, localUpload.total)}</small>
      </div>
    {/if}
    
    <div class="sizes-info">
      <span>{$t('update.maxApp', { size: humanSize(status.appMaxSize || status.firmwareMaxSize) })}</span>
      <span>{$t('update.maxWebui', { size: humanSize(status.webuiMaxSize) })}</span>
    </div>
  </article>
</section>

<style>
  .update-grid {
    display: grid;
    gap: 16px;
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .status-card,
  .action-card {
    border: 1px solid var(--surface-border);
    border-radius: 18px;
    background: var(--card-grad);
    padding: 18px;
    box-shadow: var(--shadow);
  }

  .hero {
    grid-column: 1 / -1;
    display: grid;
    gap: 16px;
    position: relative;
  }

  .eyebrow {
    display: inline-block;
    margin-bottom: 6px;
    color: var(--text-muted);
    font-size: 0.74rem;
    letter-spacing: 0.18em;
    text-transform: uppercase;
  }

  h2,
  h3 {
    margin: 0 0 8px;
  }

  p {
    margin: 0;
    color: var(--text-muted);
    line-height: 1.5;
  }

  strong {
    color: var(--text-main);
  }

  .manifest-box {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 12px;
  }

  .manifest-box div,
  .status-box {
    border: 1px solid var(--surface-border);
    border-radius: 14px;
    background: var(--surface-2);
    padding: 14px;
  }

  .manifest-box span,
  .status-head span {
    display: block;
    color: var(--text-muted);
    font-size: 0.8rem;
    margin-bottom: 4px;
  }

  .manifest-box small,
  small {
    color: var(--text-muted);
  }

  .manifest-badge {
    position: absolute;
    top: 18px;
    right: 18px;
    max-width: min(44ch, calc(100% - 36px));
    padding: 8px 12px;
    border-radius: 999px;
    font-size: 0.78rem;
    line-height: 1.25;
    backdrop-filter: blur(10px);
    border: 1px solid var(--surface-border);
    background: rgba(255, 255, 255, 0.08);
  }

  .manifest-badge.success {
    color: #9ad9a8;
  }

  .manifest-badge.warning {
    color: #ffb58f;
    border-color: rgba(255, 181, 143, 0.25);
    background: rgba(99, 49, 26, 0.28);
  }

  .version-row {
    display: flex;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
    margin-top: 8px;
  }

  .version-item {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .version-label {
    font-size: 0.75rem;
    color: var(--text-muted);
    text-transform: uppercase;
    letter-spacing: 0.05em;
  }

  .version-link {
    font-size: 0.95rem;
    font-weight: 600;
    color: #62b8dd;
    text-decoration: none;
    cursor: pointer;
    transition: color 0.2s ease;
  }

  .version-link:hover {
    color: #7ec9eb;
    text-decoration: underline;
  }

  .version-separator {
    width: 32px;
    height: 1px;
    border-radius: 999px;
    background: linear-gradient(90deg, transparent 0%, rgba(98, 184, 221, 0.6) 50%, transparent 100%);
  }

  .status-badge {
    padding: 6px 12px;
    border-radius: 6px;
    font-size: 0.8rem;
    font-weight: 600;
    background: rgba(255, 255, 255, 0.08);
  }

  .repo-status-badge {
    position: absolute;
    top: 18px;
    right: 18px;
  }

  .local-selection-badge {
    justify-self: start;
  }

  .status-badge.success {
    color: #57b67b;
    border: 1px solid rgba(87, 182, 123, 0.3);
  }

  .status-badge.info {
    color: var(--text-muted);
    border: 1px solid var(--surface-border);
  }

  .repo-card {
    position: relative;
  }

  .status-box.busy {
    outline: 1px solid rgba(98, 184, 221, 0.35);
  }

  .status-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
    margin-bottom: 10px;
  }

  .progress-track {
    height: 10px;
    margin: 10px 0 6px;
    border-radius: 999px;
    background: rgba(255, 255, 255, 0.08);
    overflow: hidden;
  }

  .progress-fill {
    height: 100%;
    border-radius: 999px;
    background: linear-gradient(90deg, #57b67b 0%, #62b8dd 100%);
    transition: width 0.2s ease;
  }

  .action-card {
    display: grid;
    gap: 12px;
    align-content: start;
  }

  .inline-progress {
    display: grid;
    gap: 6px;
    padding: 10px 12px;
    border-radius: 12px;
    border: 1px solid var(--surface-border);
    background: rgba(255, 255, 255, 0.05);
  }

  .inline-progress-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
  }

  .inline-progress-head span {
    color: var(--text-muted);
    font-size: 0.82rem;
  }

  .mini-track {
    height: 8px;
    border-radius: 999px;
    background: rgba(255, 255, 255, 0.08);
    overflow: hidden;
  }

  .mini-fill {
    height: 100%;
    border-radius: 999px;
    background: linear-gradient(90deg, #57b67b 0%, #62b8dd 100%);
    transition: width 0.18s ease;
  }

  button,
  .release-link {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-height: 42px;
    border-radius: 12px;
    border: 1px solid var(--surface-border);
    background: var(--button-bg);
    color: var(--button-text);
    font-weight: 700;
    text-decoration: none;
    cursor: pointer;
    transition: transform 0.18s ease, background 0.18s ease;
  }

  button:hover:not(:disabled),
  .release-link:hover {
    transform: translateY(-1px);
    background: var(--button-active-bg);
    color: var(--button-active-text);
  }

  button:disabled {
    opacity: 0.55;
    cursor: not-allowed;
  }

  .button-group {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 12px;
  }

  .button-group button {
    width: 100%;
  }

  .secondary-button {
    margin-top: 10px;
    min-height: 38px;
    background: transparent;
  }

  .sizes-info {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 12px;
    padding: 10px 12px;
    border-radius: 10px;
    background: rgba(255, 255, 255, 0.04);
    font-size: 0.8rem;
  }

  .sizes-info span {
    color: var(--text-muted);
  }

  input[type='file'] {
    width: 100%;
    color: var(--text-main);
  }

  @media (max-width: 840px) {
    .update-grid,
    .manifest-box {
      grid-template-columns: 1fr;
    }

    .hero {
      padding-top: 64px;
    }

    .manifest-badge {
      left: 18px;
      right: 18px;
      max-width: none;
    }

    .button-group {
      grid-template-columns: 1fr;
    }

    .sizes-info {
      grid-template-columns: 1fr;
    }
  }
</style>