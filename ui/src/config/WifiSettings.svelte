<script>
  import { onMount } from 'svelte';
  import { adminFetch } from '../adminAuth.js';
  import { showNotice } from '../dialogStore.js';
  import FieldLabel from '../FieldLabel.svelte';
  import { createTranslator } from '../i18n.js';
  import PasswordInput from '../PasswordInput.svelte';
  import ConfigPanel from './ConfigPanel.svelte';
  import { isMaskedPassword, PASSWORD_MASK } from './helpers.js';

  let { lang = 'de', onDirtyStateChange = () => {} } = $props();
  const t = $derived(createTranslator(lang));

  let wifi = $state({
    ssid: '',
    password: '',
    deviceName: 'Stand',
    ntpServerPrimary: 'pool.ntp.org',
    ntpServerSecondary: 'time.cloudflare.com',
    staticIp: { ip: '', gateway: '', subnet: '', dns: '' }
  });
  let hasPassword = $state(false);
  let mode = $state('dhcp');
  let networks = $state([]);
  let scanLoading = $state(false);
  let scanError = $state('');
  let dirty = $state(false);
  let saving = $state(false);
  let attempted = $state(false);
  let touched = $state({});

  let ntpTesting = $state(false);
  let ntpResults = $state([]);  // [{ ok, text }]

  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

  async function testNtpHost(host) {
    const errors = t('config.wifi.ntpErr');
    const reason = (code) => errors[code] || errors.default;
    try {
      const start = await adminFetch(`/api/ntp/test?host=${encodeURIComponent(host)}`, { method: 'POST' });
      if (!start.ok) return { ok: false, text: t('config.wifi.ntpTestFail', { host, reason: reason('busy_or_invalid') }) };
      for (let i = 0; i < 20; i++) {
        await sleep(500);
        const res = await adminFetch('/api/ntp/test', { cache: 'no-store' });
        const r = await res.json();
        if (!r.finished) continue;
        if (!r.ok) return { ok: false, text: t('config.wifi.ntpTestFail', { host, reason: reason(r.error) }) };
        const skew = r.deviceEpoch ? Math.abs(r.deviceEpoch - r.serverEpoch) : 0;
        return {
          ok: true,
          text: skew > 5
            ? t('config.wifi.ntpTestOkOff', { host, ms: r.roundTripMs, sec: skew })
            : t('config.wifi.ntpTestOk', { host, ms: r.roundTripMs })
        };
      }
      return { ok: false, text: t('config.wifi.ntpTestFail', { host, reason: reason('timeout') }) };
    } catch (_) {
      return { ok: false, text: t('config.wifi.ntpTestFail', { host, reason: reason('default') }) };
    }
  }

  async function testNtp() {
    if (ntpTesting) return;
    const hosts = [wifi.ntpServerPrimary, wifi.ntpServerSecondary].map((h) => String(h || '').trim()).filter(Boolean);
    ntpResults = [];
    if (hosts.length === 0) {
      ntpResults = [{ ok: false, text: t('config.wifi.ntpTestNone') }];
      return;
    }
    ntpTesting = true;
    try {
      for (const host of hosts) {
        ntpResults = [...ntpResults, await testNtpHost(host)];
      }
    } finally {
      ntpTesting = false;
    }
  }

  let manualTime = $state('');  // datetime-local, Ortszeit des Browsers
  let timeBusy = $state(false);

  function pad(n) { return String(n).padStart(2, '0'); }
  function toLocalInput(date) {
    return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}T${pad(date.getHours())}:${pad(date.getMinutes())}`;
  }
  $effect(() => { if (!manualTime) manualTime = toLocalInput(new Date()); });

  async function setDeviceTime(date) {
    if (timeBusy || Number.isNaN(date.getTime())) return;
    timeBusy = true;
    try {
      const epoch = Math.floor(date.getTime() / 1000);
      const response = await adminFetch(`/api/time?epoch=${epoch}`, { method: 'POST' });
      if (!response.ok) throw new Error('http');
      showNotice('success', t('config.wifi.timeSet'));
    } catch (_) {
      showNotice('error', t('config.wifi.timeSetFailed'));
    } finally {
      timeBusy = false;
    }
  }

  const REQUIRED_FIELDS = ['ip', 'subnet', 'dns'];
  const FIELD_ERROR_KEYS = { ip: 'errorIp', subnet: 'errorSubnet', dns: 'errorDns' };

  // Wie die Firmware: Gerätename -> DNS-Hostname (nur a-z, 0-9 und Bindestrich).
  const localUrl = $derived.by(() => {
    const host = String(wifi.deviceName || '')
      .trim()
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/^-+|-+$/g, '')
      .slice(0, 63);
    return `http://${host || 'stand'}/`;
  });

  function fieldError(field) {
    const required = mode === 'static' && REQUIRED_FIELDS.includes(field);
    const empty = !String(wifi.staticIp[field] || '').trim();
    if (!required || !empty || !(attempted || touched[field])) return '';
    return t(`config.wifi.${FIELD_ERROR_KEYS[field] || 'errorRequired'}`);
  }

  async function load() {
    const response = await adminFetch('/api/wifi', { cache: 'no-store' });
    const c = await response.json();
    if (dirty) return;
    wifi = {
      ssid: c.ssid || '',
      password: isMaskedPassword(c.password || '') ? '' : (c.password || ''),
      deviceName: c.deviceName || 'Stand',
      ntpServerPrimary: c.ntpServerPrimary || 'pool.ntp.org',
      ntpServerSecondary: c.ntpServerSecondary || 'time.cloudflare.com',
      staticIp: {
        ip: c.staticIp?.ip || '',
        gateway: c.staticIp?.gateway || '',
        subnet: c.staticIp?.subnet || '',
        dns: c.staticIp?.dns || ''
      }
    };
    hasPassword = (c.hasPassword ?? false) || isMaskedPassword(c.password || '');
    mode = c.useStaticIp ? 'static' : 'dhcp';
    attempted = false;
    touched = {};
  }

  async function scan() {
    scanLoading = true;
    scanError = '';
    try {
      const response = await adminFetch('/api/wifi/scan', { method: 'POST', cache: 'no-store' });
      if (!response.ok) {
        scanError = t('config.wifi.scanFailed');
        return;
      }
      networks = (await response.json()).networks || [];
      if (networks.length === 0) scanError = t('config.wifi.noNetworks');
    } catch (_) {
      scanError = t('config.wifi.scanFailed');
    } finally {
      scanLoading = false;
    }
  }

  function selectScanned(event) {
    if (event.currentTarget.value) wifi.ssid = event.currentTarget.value;
  }

  function useStaticIp() {
    mode = 'static';
    if (!wifi.staticIp.subnet) wifi.staticIp.subnet = '255.255.255.0';
  }

  function useDhcp() {
    mode = 'dhcp';
    attempted = false;
    touched = {};
  }

  async function save() {
    if (saving) return false;
    if (mode === 'static') {
      attempted = true;
      if (!wifi.staticIp.ip || !wifi.staticIp.subnet || !wifi.staticIp.dns) {
        showNotice('error', t('config.wifi.staticRequired'));
        return false;
      }
    }

    const isStatic = mode === 'static';
    saving = true;
    try {
      const response = await adminFetch('/api/wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          ssid: wifi.ssid,
          password: wifi.password,
          deviceName: wifi.deviceName,
          ntpServerPrimary: wifi.ntpServerPrimary,
          ntpServerSecondary: wifi.ntpServerSecondary,
          useStaticIp: isStatic,
          staticIp: {
            ip: isStatic ? wifi.staticIp.ip : '',
            gateway: isStatic ? wifi.staticIp.gateway : '',
            subnet: isStatic ? wifi.staticIp.subnet : '',
            dns: isStatic ? wifi.staticIp.dns : ''
          }
        })
      });
      if (!response.ok) {
        showNotice('error', t('config.wifi.saveFailed'));
        return false;
      }
      showNotice('success', t('config.wifi.saved'));
      dirty = false;
      attempted = false;
      touched = {};
      return true;
    } catch (_) {
      showNotice('error', t('config.wifi.saveFailed'));
      return false;
    } finally {
      saving = false;
    }
  }

  function discard() {
    dirty = false;
    attempted = false;
    touched = {};
    load();
  }

  $effect(() => {
    onDirtyStateChange(dirty);
  });

  onMount(() => { load(); });
</script>

<ConfigPanel {t} title={t('config.wifi.title')} {dirty} {saving} onsave={save} ondiscard={discard} onedit={() => { dirty = true; }}>
  <label class="field-row">
    <span>{t('config.wifi.ssid')}</span>
    <div class="input-action-row">
      <input bind:value={wifi.ssid} />
      <button class="icon-button" type="button" onclick={scan} aria-label={t('config.wifi.scan')} title={t('config.wifi.scan')}>
        <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M10 4a6 6 0 1 0 3.87 10.58l4.27 4.28 1.42-1.42-4.28-4.27A6 6 0 0 0 10 4zm0 2a4 4 0 1 1 0 8 4 4 0 0 1 0-8z"/></svg>
      </button>
    </div>
  </label>
  {#if scanLoading}
    <p class="helper-text">{t('config.wifi.scanning')}</p>
  {:else if scanError}
    <p class="helper-text error">{scanError}</p>
  {/if}
  {#if networks.length > 0}
    <label class="field-row compact-row">
      <span>{t('config.wifi.foundNetworks')}</span>
      <select onchange={selectScanned}>
        <option value="">{t('config.wifi.chooseNetwork')}</option>
        {#each networks as network}
          <option value={network.ssid}>{network.ssid} ({network.rssi} dBm)</option>
        {/each}
      </select>
    </label>
  {/if}

  <div class="field-row">
    <span>{t('config.common.password')}</span>
    <PasswordInput bind:value={wifi.password} hasStoredPassword={hasPassword} mask={PASSWORD_MASK} />
  </div>

  <label class="field-row">
    <span>{t('config.wifi.deviceName')}</span>
    <input bind:value={wifi.deviceName} placeholder={t('config.wifi.deviceNamePlaceholder')} />
  </label>
  <p class="helper-text">{t('config.wifi.reachableAt', { url: localUrl })}</p>

  <div class="choice-grid" role="radiogroup" aria-label={t('config.wifi.ipConfig')}>
    <label class="choice-card">
      <input type="radio" name="wifi-ip-mode" checked={mode === 'dhcp'} onchange={useDhcp} />
      <span>{t('config.wifi.dhcp')}</span>
    </label>
    <label class="choice-card">
      <input type="radio" name="wifi-ip-mode" checked={mode === 'static'} onchange={useStaticIp} />
      <span>{t('config.wifi.staticIp')}</span>
    </label>
  </div>

  <h3>{t('config.wifi.ntp')}</h3>
  <label class="field-row">
    <span>{t('config.wifi.ntpServer1')}</span>
    <input bind:value={wifi.ntpServerPrimary} placeholder="pool.ntp.org" />
  </label>
  <label class="field-row">
    <span>{t('config.wifi.ntpServer2')}</span>
    <input bind:value={wifi.ntpServerSecondary} placeholder="time.cloudflare.com" />
  </label>
  <p class="helper-text">{t('config.wifi.ntpHint')}</p>
  <div class="diag-section">
    <button class="secondary-sm" type="button" onclick={testNtp} disabled={ntpTesting}>
      {ntpTesting ? t('config.wifi.ntpTesting') : t('config.wifi.ntpTest')}
    </button>
    {#each ntpResults as result}
      <p class="helper-text" class:error={!result.ok}>{result.ok ? '✓' : '✗'} {result.text}</p>
    {/each}
  </div>

  <h3>{t('config.wifi.manualTime')}</h3>
  <p class="helper-text">{t('config.wifi.manualTimeHint')}</p>
  <label class="field-row">
    <span>{t('config.wifi.manualTimeLabel')}</span>
    <input type="datetime-local" bind:value={manualTime} />
  </label>
  <div class="button-row">
    <button class="secondary-sm" type="button" onclick={() => setDeviceTime(new Date(manualTime))} disabled={timeBusy || !manualTime}>{t('config.wifi.timeApply')}</button>
    <button class="secondary-sm" type="button" onclick={() => setDeviceTime(new Date())} disabled={timeBusy}>{t('config.wifi.timeFromBrowser')}</button>
  </div>

  {#if mode === 'static'}
    {#each [['ip', 'staticIpLabel', true], ['gateway', 'gateway', false], ['subnet', 'subnet', true], ['dns', 'dns', true]] as [field, labelKey, required]}
      <label class="field-row field-row--top-align">
        <span><FieldLabel text={t(`config.wifi.${labelKey}`)} {required} /></span>
        <div class="field-control">
          <input
            bind:value={wifi.staticIp[field]}
            aria-required={required ? 'true' : undefined}
            aria-invalid={fieldError(field) ? 'true' : undefined}
            class:input-invalid={fieldError(field)}
            onblur={() => { touched = { ...touched, [field]: true }; }}
          />
          {#if fieldError(field)}
            <p class="field-error">{fieldError(field)}</p>
          {/if}
        </div>
      </label>
    {/each}
  {/if}

  <button class="primary" onclick={save}>{t('config.common.save')}</button>
</ConfigPanel>
