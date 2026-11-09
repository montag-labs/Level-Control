<script>
  import { onMount } from 'svelte';
  import { adminFetch } from '../adminAuth.js';
  import { showNotice } from '../dialogStore.js';
  import { createTranslator } from '../i18n.js';
  import PasswordInput from '../PasswordInput.svelte';
  import { detectProvider, SMTP_PROVIDERS } from '../smtpProviders.js';
  import ConfigPanel from './ConfigPanel.svelte';
  import { isMaskedPassword, PASSWORD_MASK, runPushJob } from './helpers.js';

  let { lang = 'de', onDirtyStateChange = () => {} } = $props();
  const t = $derived(createTranslator(lang));

  // Standardtexte der E-Mail (Inhalt der Nachricht, unabhängig von der UI-Sprache)
  const DEFAULT_SENDER_NAME = 'Stand Control';
  const DEFAULT_SUBJECT = 'Stand Control Warnung: Stand hat {level_percent}% erreicht. Salz nachfüllen!';
  const DEFAULT_BODY = 'Der Füllstand hat {level_percent}% ({level_cm} cm) erreicht.\nBitte Salz nachfüllen!\nDein Stand Control';

  let push = $state({
    enabled: false,
    smtpServer: '',
    smtpPort: 587,
    useSsl: false,
    startTls: false,
    smtpSkipCertVerify: true,
    authUser: '',
    authPassword: '',
    senderName: DEFAULT_SENDER_NAME,
    senderEmail: '',
    recipientEmail: '',
    triggerPercent: 20,
    sendHour: 8,
    sendMinute: 0,
    reminderCycle: 'day',
    reminderWeekday: 1,
    subjectTemplate: DEFAULT_SUBJECT,
    bodyTemplate: DEFAULT_BODY
  });
  let hasAuthPassword = $state(false);
  let encryptionMode = $state('none');
  let selectedProvider = $state('custom');
  let diagnostic = $state(null);
  let diagnosticRunning = $state(false);
  let testRunning = $state(false);
  let dirty = $state(false);
  let saving = $state(false);

  const TIME_OPTIONS = Array.from({ length: 96 }, (_, index) => toTimeString(Math.floor(index / 4), (index % 4) * 15));
  const weekdays = $derived(t('config.push.weekdays'));
  const reminderText = $derived(
    t(push.reminderCycle === 'week' ? 'config.push.reminderWeek' : push.reminderCycle === 'month' ? 'config.push.reminderMonth' : 'config.push.reminderDay')
  );

  function toTimeString(hour, minute) {
    const h = Math.max(0, Math.min(23, Number(hour) || 0));
    const m = Math.max(0, Math.min(59, Number(minute) || 0));
    return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`;
  }

  function parseTime(value) {
    const [rawHour, rawMinute] = String(value || '').split(':');
    return { hour: Math.max(0, Math.min(23, Number(rawHour) || 0)), minute: Math.max(0, Math.min(59, Number(rawMinute) || 0)) };
  }

  function normalizeCycle(value) {
    return ['day', 'week', 'month'].includes(value) ? value : 'day';
  }

  function cycleFromLegacyMinutes(value) {
    const minutes = Number(value) || 1440;
    return minutes >= 43200 ? 'month' : minutes >= 10080 ? 'week' : 'day';
  }

  function normalizeWeekday(value) {
    const number = Number(value);
    return Number.isFinite(number) && number >= 1 && number <= 7 ? Math.round(number) : 1;
  }

  function normalizeEncryption(value) {
    const mode = String(value || '').trim().toLowerCase();
    if (mode === 'ssl' || mode === 'tls') return 'ssl';
    if (mode === 'starttls' || mode === 'start_tls' || mode === 'start-tls') return 'starttls';
    return 'none';
  }

  function parseBool(value) {
    if (typeof value === 'boolean') return value;
    if (typeof value === 'number') return value !== 0;
    return ['1', 'true', 'yes', 'on'].includes(String(value || '').trim().toLowerCase());
  }

  // SMTP-Port passend zur Verschlüsselung (465 = SSL/TLS, 587 = STARTTLS), solange er noch ein Standardwert ist.
  function portFor(useSsl, startTls, current) {
    let port = Math.max(1, Number(current) || 0);
    if (useSsl && (!port || port === 587)) port = 465;
    if (startTls && (!port || port === 465)) port = 587;
    if (!useSsl && !startTls && !port) port = 587;
    return port;
  }

  function applyEncryption(mode) {
    const normalized = normalizeEncryption(mode);
    push.useSsl = normalized === 'ssl';
    push.startTls = normalized === 'starttls';
    push.smtpPort = portFor(push.useSsl, push.startTls, push.smtpPort);
    encryptionMode = normalized;
  }

  function selectProvider(id) {
    const provider = SMTP_PROVIDERS.find((entry) => entry.id === id);
    if (!provider) return;
    selectedProvider = id;
    if (id !== 'custom') {
      push.smtpServer = provider.host;
      push.smtpPort = provider.port;
      push.useSsl = provider.security === 'ssl';
      push.startTls = provider.security === 'starttls';
      encryptionMode = normalizeEncryption(provider.security);
      dirty = true;
    }
  }

  async function load() {
    const response = await adminFetch('/api/push', { cache: 'no-store' });
    const c = await response.json();
    if (dirty) return;

    const providerId = detectProvider(c.smtpServer || '', c.smtpPort || 587);
    const provider = SMTP_PROVIDERS.find((entry) => entry.id === providerId);
    const rawMode = c.encryption ?? c.security ?? '';
    let mode = String(rawMode || '').trim() !== '' ? normalizeEncryption(rawMode) : '';
    if (!mode) {
      if (parseBool(c.useSsl)) mode = 'ssl';
      else if (parseBool(c.startTls)) mode = 'starttls';
      else if (provider?.security) mode = normalizeEncryption(provider.security);
      else mode = Number(c.smtpPort) === 465 ? 'ssl' : 'none';
    }
    const useSsl = mode === 'ssl';

    push = {
      enabled: c.enabled ?? false,
      smtpServer: c.smtpServer || '',
      smtpPort: c.smtpPort || 587,
      useSsl,
      startTls: useSsl ? false : mode === 'starttls',
      smtpSkipCertVerify: true,
      authUser: c.authUser || '',
      authPassword: isMaskedPassword(c.authPassword || '') ? '' : (c.authPassword || ''),
      senderName: c.senderName || DEFAULT_SENDER_NAME,
      senderEmail: c.senderEmail || '',
      recipientEmail: c.recipientEmail || '',
      triggerPercent: c.triggerPercent ?? 20,
      sendHour: c.sendHour ?? 8,
      sendMinute: c.sendMinute ?? 0,
      reminderCycle: normalizeCycle(c.reminderCycle || cycleFromLegacyMinutes(c.cycleMinutes)),
      reminderWeekday: normalizeWeekday(c.reminderWeekday ?? 1),
      subjectTemplate: c.subjectTemplate || DEFAULT_SUBJECT,
      bodyTemplate: c.bodyTemplate || DEFAULT_BODY
    };
    encryptionMode = useSsl ? 'ssl' : (mode === 'starttls' ? 'starttls' : 'none');
    hasAuthPassword = (c.hasAuthPassword ?? false) || isMaskedPassword(c.authPassword || '');
    selectedProvider = providerId;
  }

  async function save() {
    if (saving) return false;
    const triggerPercent = Math.max(0, Math.min(100, Number(push.triggerPercent) || 0));
    const time = parseTime(toTimeString(push.sendHour, push.sendMinute));
    const useSsl = Boolean(push.useSsl);
    const startTls = !useSsl && Boolean(push.startTls);
    const reminderCycle = normalizeCycle(push.reminderCycle);
    const reminderWeekday = normalizeWeekday(push.reminderWeekday);
    const smtpPort = portFor(useSsl, startTls, push.smtpPort);

    saving = true;
    try {
      const response = await adminFetch('/api/push', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          ...push,
          smtpSkipCertVerify: true,
          smtpPort,
          encryption: useSsl ? 'ssl' : (startTls ? 'starttls' : 'none'),
          useSsl,
          startTls,
          triggerPercent,
          cycleMinutes: reminderCycle === 'week' ? 10080 : reminderCycle === 'month' ? 43200 : 1440,
          reminderCycle,
          reminderWeekday,
          sendHour: time.hour,
          sendMinute: time.minute
        })
      });
      if (!response.ok) {
        showNotice('error', t('config.push.saveFailed'));
        return false;
      }
      push = { ...push, smtpPort, useSsl, startTls, triggerPercent, reminderCycle, reminderWeekday, sendHour: time.hour, sendMinute: time.minute };
      showNotice('success', t('config.push.saved'));
      dirty = false;
      return true;
    } catch (_) {
      showNotice('error', t('config.push.saveFailed'));
      return false;
    } finally {
      saving = false;
    }
  }

  function discard() {
    dirty = false;
    load();
  }

  function describeJobError(error) {
    return error?.code === 'busy' ? t('config.push.testBusy') : (error?.message || t('config.push.noDeviceReply'));
  }

  async function sendTest() {
    if (testRunning || !(await save())) return;
    testRunning = true;
    try {
      const job = await runPushJob('/api/push/test');
      if (job.success) {
        showNotice('success', t('config.push.testSent'));
      } else {
        showNotice('error', job.error || t('config.push.testFailed'));
      }
    } catch (error) {
      showNotice('error', describeJobError(error));
    } finally {
      testRunning = false;
    }
  }

  async function runDiagnostic() {
    if (diagnosticRunning) return;
    diagnosticRunning = true;
    diagnostic = null;
    try {
      if (dirty && !(await save())) return;
      const job = await runPushJob('/api/push/smtp-check');
      diagnostic = { success: job.success, steps: job.steps || [] };
    } catch (error) {
      diagnostic = { success: false, steps: [{ name: t('config.push.deviceStep'), ok: false, detail: describeJobError(error) }] };
    } finally {
      diagnosticRunning = false;
    }
  }

  $effect(() => {
    onDirtyStateChange(dirty);
  });

  onMount(() => { load(); });
</script>

<ConfigPanel {t} title={t('config.push.title')} {dirty} {saving} onsave={save} ondiscard={discard} onedit={() => { dirty = true; }}>
  <label class="checkbox-row"><input type="checkbox" bind:checked={push.enabled} /> {t('config.push.enable')}</label>

  <label class="field-row">
    <span>{t('config.push.provider')}</span>
    <select class="theme-select" value={selectedProvider} onchange={(event) => selectProvider(event.currentTarget.value)}>
      {#each SMTP_PROVIDERS as provider}
        <option value={provider.id}>{provider.id === 'custom' ? t('config.push.providerCustom') : provider.label}</option>
      {/each}
    </select>
  </label>

  <label class="field-row">
    <span>{t('config.push.smtpServer')}</span>
    <input bind:value={push.smtpServer} oninput={() => { selectedProvider = detectProvider(push.smtpServer, push.smtpPort); }} />
  </label>
  <label class="field-row">
    <span>{t('config.push.smtpPort')}</span>
    <input type="number" min="1" bind:value={push.smtpPort} oninput={() => { selectedProvider = detectProvider(push.smtpServer, push.smtpPort); }} />
  </label>

  <label class="field-row field-row--top-align">
    <span>{t('config.push.encryption')}</span>
    <div class="field-control field-control--choice">
      <select class="theme-select" aria-label={t('config.push.encryptionAria')} bind:value={encryptionMode} onchange={() => applyEncryption(encryptionMode)}>
        <option value="none">{t('config.push.encNone')}</option>
        <option value="ssl">{t('config.push.encSsl')}</option>
        <option value="starttls">{t('config.push.encStarttls')}</option>
      </select>
      {#if encryptionMode === 'starttls'}
        <p class="helper-text error">{t('config.push.starttlsUnsupported')}</p>
      {/if}
    </div>
  </label>

  <label class="field-row"><span>{t('config.push.smtpUser')}</span><input bind:value={push.authUser} /></label>
  <div class="field-row">
    <span>{t('config.push.smtpPassword')}</span>
    <PasswordInput bind:value={push.authPassword} hasStoredPassword={hasAuthPassword} mask={PASSWORD_MASK} />
  </div>

  <div class="diag-section">
    <button class="secondary-sm" type="button" onclick={runDiagnostic} disabled={diagnosticRunning}>
      {diagnosticRunning ? t('config.push.checking') : t('config.push.checkConnection')}
    </button>
    {#if diagnostic}
      <div class="diag-result" class:diag-result--ok={diagnostic.success} class:diag-result--fail={!diagnostic.success}>
        <p class="diag-summary">{diagnostic.success ? t('config.push.diagOk') : t('config.push.diagFail')}</p>
        <ul class="diag-steps">
          {#each diagnostic.steps as step}
            <li class="diag-step" class:diag-step--fail={!step.ok}>
              <span class="diag-icon" aria-hidden="true">{step.ok ? '✓' : '✗'}</span>
              <span class="diag-step-name">{step.name}</span>
              <span class="diag-step-detail">{step.detail}</span>
            </li>
          {/each}
        </ul>
      </div>
    {/if}
  </div>

  <h3>{t('config.push.senderRecipient')}</h3>
  <label class="field-row"><span>{t('config.push.senderName')}</span><input bind:value={push.senderName} /></label>
  <label class="field-row"><span>{t('config.push.senderEmail')}</span><input type="email" bind:value={push.senderEmail} /></label>
  <label class="field-row"><span>{t('config.push.recipientEmail')}</span><input type="email" bind:value={push.recipientEmail} /></label>

  <h3>{t('config.push.trigger')}</h3>
  <label class="field-row"><span>{t('config.push.threshold')}</span><input type="number" min="0" max="100" step="0.1" bind:value={push.triggerPercent} /></label>

  <h3>{t('config.push.schedule')}</h3>
  <p class="helper-text">{t('config.push.scheduleHint')}</p>
  <label class="field-row">
    <span>{t('config.push.cycle')}</span>
    <select class="theme-select" bind:value={push.reminderCycle}>
      <option value="day">{t('config.push.cycleDay')}</option>
      <option value="week">{t('config.push.cycleWeek')}</option>
      <option value="month">{t('config.push.cycleMonth')}</option>
    </select>
  </label>
  <label class="field-row">
    <span>{t('config.push.time')}</span>
    <select
      class="theme-select time-select"
      value={toTimeString(push.sendHour, push.sendMinute)}
      onchange={(event) => {
        const parsed = parseTime(event.currentTarget.value);
        push.sendHour = parsed.hour;
        push.sendMinute = parsed.minute;
      }}
    >
      {#each TIME_OPTIONS as option}<option value={option}>{option}</option>{/each}
    </select>
  </label>
  <label class="field-row">
    <span>{t('config.push.weekday')}</span>
    <select class="theme-select" bind:value={push.reminderWeekday} disabled={push.reminderCycle === 'day'}>
      {#each weekdays as name, index}<option value={index + 1}>{name}</option>{/each}
    </select>
  </label>
  <p class="helper-text">{reminderText} {t('config.push.timeNote')}</p>

  <h3>{t('config.push.template')}</h3>
  <label class="field-row"><span>{t('config.push.subject')}</span><input bind:value={push.subjectTemplate} /></label>
  <label class="field-row field-row--top-align"><span>{t('config.push.body')}</span><textarea rows="6" bind:value={push.bodyTemplate}></textarea></label>
  <p class="helper-text">
    {t('config.push.placeholders')}
    {#each ['level_percent', 'level_cm', 'raw_distance_m', 'timestamp', 'device_id', 'ip', 'ssid'] as name, index}
      {index > 0 ? ', ' : ''}<code>{'{' + name + '}'}</code>
    {/each}
  </p>

  <div class="button-row button-row--push">
    <button class="primary" onclick={save}>{t('config.common.save')}</button>
    <button class="secondary-sm" type="button" onclick={sendTest} disabled={testRunning}>{t('config.push.sendTest')}</button>
  </div>
</ConfigPanel>
