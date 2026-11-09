<script>
  import { onMount } from 'svelte';
  import { adminFetch } from '../adminAuth.js';
  import { showNotice } from '../dialogStore.js';
  import FieldLabel from '../FieldLabel.svelte';
  import { createTranslator } from '../i18n.js';
  import PasswordInput from '../PasswordInput.svelte';
  import ConfigPanel from './ConfigPanel.svelte';
  import { isMaskedPassword, PASSWORD_MASK } from './helpers.js';

  let { data, lang = 'de', onDirtyStateChange = () => {} } = $props();
  const t = $derived(createTranslator(lang));

  let mqtt = $state({ server: '', port: 1883, user: '', password: '', discovery: true });
  let hasPassword = $state(false);
  let deviceId = $state('');
  let dirty = $state(false);
  let saving = $state(false);

  const mqttState = $derived(data?.mqttState || 'unknown');
  const stateKeys = {
    connected: 'stateConnected',
    connecting: 'stateConnecting',
    backoff: 'stateBackoff',
    disconnected: 'stateDisconnected',
    uninitialized: 'stateUninitialized'
  };
  const stateLabel = $derived(t(`config.mqtt.${stateKeys[mqttState] || 'stateUnknown'}`));
  const topicId = $derived(deviceId || '{deviceId}');

  const DISCOVERY_GROUPS = [
    { key: 'groupValues', entries: [['sensor', 'fill_level'], ['sensor', 'distance_cm'], ['sensor', 'raw_distance'], ['sensor', 'ping_us']] },
    { key: 'groupConfig', entries: [['number', 'behaelterhoehe'], ['number', 'offset'], ['number', 'sample_interval']] },
    {
      key: 'groupSystem',
      entries: [['sensor', 'rssi'], ['sensor', 'ip_address'], ['sensor', 'ssid'], ['sensor', 'uptime'], ['sensor', 'free_heap'], ['sensor', 'cpu_freq']]
    },
    { key: 'groupOta', entries: [['update', 'ota']] }
  ];
  const STATUS_TOPICS = [
    ['level-control/status', 'topicStatus'],
    ['level-control/sensor/state', 'topicSensor'],
    ['level-control/system/state', 'topicSystem'],
    ['level-control/update/state', 'topicUpdateState'],
    ['level-control/update/install', 'topicUpdateInstall']
  ];

  async function load() {
    const response = await adminFetch('/api/mqtt', { cache: 'no-store' });
    const c = await response.json();
    if (dirty) return;
    mqtt = {
      server: c.server || '',
      port: c.port || 1883,
      user: c.user || '',
      password: isMaskedPassword(c.password || '') ? '' : (c.password || ''),
      discovery: c.discovery ?? true
    };
    hasPassword = (c.hasPassword ?? false) || isMaskedPassword(c.password || '');
    deviceId = c.device_id || '';
  }

  async function save() {
    if (saving) return false;
    saving = true;
    try {
      const response = await adminFetch('/api/mqtt', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(mqtt)
      });
      if (!response.ok) {
        showNotice('error', t('config.mqtt.saveFailed'));
        return false;
      }
      showNotice('success', t('config.mqtt.saved'));
      dirty = false;
      return true;
    } catch (_) {
      showNotice('error', t('config.mqtt.saveFailed'));
      return false;
    } finally {
      saving = false;
    }
  }

  function discard() {
    dirty = false;
    load();
  }

  async function reconnect() {
    try {
      await adminFetch('/api/mqtt/reconnect', { method: 'POST' });
    } catch (_) { /* Status kommt ohnehin über den WebSocket */ }
  }

  let mqttTesting = $state(false);
  let mqttTestResult = $state(null);  // { ok, text }

  // Speichert bei Bedarf, löst eine neue Verbindung aus und wartet bis zu 10 s auf "connected".
  async function testConnection() {
    if (mqttTesting) return;
    mqttTesting = true;
    mqttTestResult = null;
    try {
      if (dirty && !(await save())) return;
      const reconnect = await adminFetch('/api/mqtt/reconnect', { method: 'POST' });
      if (!reconnect.ok) throw new Error('reconnect');
      let state = 'unknown';
      // bis zu 15 s warten; Status sowohl per Abfrage als auch aus dem Live-Wert (WebSocket) lesen
      for (let i = 0; i < 30; i++) {
        await new Promise((resolve) => setTimeout(resolve, 500));
        try {
          const res = await fetch('/api/mqtt/status', { cache: 'no-store' });
          if (res.ok) state = (await res.json()).state || state;
        } catch (_) { /* nächster Versuch */ }
        if (mqttState === 'connected') state = 'connected';
        if (state === 'connected') break;
      }
      mqttTestResult = state === 'connected'
        ? { ok: true, text: t('config.mqtt.testOk', { server: mqtt.server, port: mqtt.port }) }
        : { ok: false, text: t('config.mqtt.testFail', { server: mqtt.server, port: mqtt.port }) + ` (${state})` };
    } catch (error) {
      mqttTestResult = { ok: false, text: `${t('config.mqtt.testError')} ${error?.message || ''}`.trim() };
    } finally {
      mqttTesting = false;
    }
  }

  $effect(() => {
    onDirtyStateChange(dirty);
  });

  onMount(() => { load(); });
</script>

<ConfigPanel {t} title={t('config.mqtt.title')} {dirty} {saving} onsave={save} ondiscard={discard} onedit={() => { dirty = true; }}>
  <div class="mqtt-status-row">
    <span class="mqtt-dot mqtt-dot--{mqttState}" aria-hidden="true"></span>
    <span class="mqtt-state-text">{stateLabel}</span>
    {#if mqttState !== 'connected'}
      <button class="secondary-sm" type="button" onclick={reconnect}>{t('config.mqtt.connectNow')}</button>
    {/if}
  </div>
  <label class="field-row"><span><FieldLabel text={t('config.mqtt.server')} /></span><input bind:value={mqtt.server} /></label>
  <label class="field-row"><span>{t('config.mqtt.port')}</span><input type="number" bind:value={mqtt.port} /></label>
  <label class="field-row"><span>{t('config.mqtt.user')}</span><input bind:value={mqtt.user} /></label>
  <div class="field-row">
    <span>{t('config.common.password')}</span>
    <PasswordInput bind:value={mqtt.password} hasStoredPassword={hasPassword} mask={PASSWORD_MASK} />
  </div>
  <label class="checkbox-row"><input type="checkbox" bind:checked={mqtt.discovery} /> {t('config.mqtt.discovery')}</label>
  <div class="button-row">
    <button class="primary" onclick={save}>{t('config.common.save')}</button>
    <button class="secondary-sm" type="button" onclick={testConnection} disabled={mqttTesting || !mqtt.server}>
      {mqttTesting ? t('config.mqtt.testing') : t('config.mqtt.testConnection')}
    </button>
  </div>
  {#if mqttTestResult}
    <p class="helper-text" class:error={!mqttTestResult.ok}>{mqttTestResult.ok ? '✓' : '✗'} {mqttTestResult.text}</p>
  {/if}

  <h3>{t('config.mqtt.haTitle')}</h3>
  {#if deviceId}
    <p class="device-id-row">{t('config.mqtt.deviceId')} <code class="device-id-code">{deviceId}</code></p>
  {/if}
  <p>{t('config.mqtt.haHint')}</p>

  <details class="topic-card">
    <summary>
      <span class="topic-card-title">{t('config.mqtt.topics')}</span>
      <span class="topic-card-meta">{t('config.mqtt.topicsMeta')}</span>
    </summary>

    <div class="topic-card-content">
      <p class="topic-heading">{t('config.mqtt.discoveryTopics')}</p>
      <ul class="topic-list">
        {#each DISCOVERY_GROUPS as group}
          <li class="topic-group">{t(`config.mqtt.${group.key}`)}</li>
          {#each group.entries as [domain, objectId]}
            <li><code>homeassistant/{domain}/{topicId}/{objectId}/config</code></li>
          {/each}
        {/each}
      </ul>

      <p class="topic-heading">{t('config.mqtt.statusTopics')}</p>
      <ul class="topic-list">
        {#each STATUS_TOPICS as [topic, descriptionKey]}
          <li><code>{topic}</code> - {t(`config.mqtt.${descriptionKey}`)}</li>
        {/each}
        {#each ['behaelterhoehe', 'offset', 'sampleinterval'] as setting}
          <li><code>level-control/config/{setting}/state</code> / <code>.../set</code></li>
        {/each}
      </ul>
    </div>
  </details>
</ConfigPanel>
