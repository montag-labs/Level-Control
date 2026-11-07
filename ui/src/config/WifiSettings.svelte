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
