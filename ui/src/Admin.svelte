<script>
  import { onMount } from 'svelte';
  import Update from './Update.svelte';
  import AdminPassword from './AdminPassword.svelte';
  import Backup from './Backup.svelte';
  import SensorSettings from './config/SensorSettings.svelte';
  import WifiSettings from './config/WifiSettings.svelte';
  import MqttSettings from './config/MqttSettings.svelte';
  import PushSettings from './config/PushSettings.svelte';
  import { ensureAdminSession } from './adminAuth.js';
  import { showNotice } from './dialogStore.js';
  import { createTranslator } from './i18n.js';

  export let data;
  export let loadConfig;
  export let currentVersion = '0.0.0';
  export let lang = 'de';
  export let onDirtyStateChange = () => {};

  let activeModule = 'sensor';
  let authenticated = false;
  let loginPending = true;
  let loginError = '';
  let activeModuleDirty = false;
  $: t = createTranslator(lang);

  const modules = [
    { id: 'sensor', key: 'sensor' },
    { id: 'wifi', key: 'wifi' },
    { id: 'mqtt_ha', key: 'mqtt_ha' },
    { id: 'push', key: 'push' },
    { id: 'backup', key: 'backup' },
    { id: 'password', key: 'password' },
    { id: 'update', key: 'update' }
  ];

  function reportDirty(dirty) {
    activeModuleDirty = dirty;
    onDirtyStateChange(dirty);
  }

  async function authenticate() {
    loginPending = true;
    loginError = '';
    try {
      await ensureAdminSession();
      authenticated = true;
    } catch (error) {
      authenticated = false;
      loginError = error?.message || t('admin.loginFailed');
      showNotice('error', loginError);
    } finally {
      loginPending = false;
    }
  }

  async function requestModuleChange(nextModule) {
    if (nextModule === activeModule) return;
    if (activeModuleDirty) {
      const confirmed = window.confirm(t('admin.unsavedConfirm'));
      if (!confirmed) return;
    }
    activeModuleDirty = false;
    onDirtyStateChange(false);
    activeModule = nextModule;
  }

  onMount(authenticate);
</script>

<section class="admin-area" aria-labelledby="admin-title">
  <header class="admin-header">
    <div>
      <span class="eyebrow">{t('admin.eyebrow')}</span>
      <h2 id="admin-title">{t('admin.title')}</h2>
      <p>{t('admin.subtitle')}</p>
    </div>
    {#if !authenticated && !loginPending}
      <button class="admin-login-button" on:click={authenticate}>{t('admin.login')}</button>
    {/if}
  </header>

  {#if loginPending}
    <div class="admin-state">{t('admin.checking')}</div>
  {:else if !authenticated}
    <div class="admin-state admin-state--error">{loginError || t('admin.loginRequired')}</div>
  {:else}
    <nav class="admin-module-nav" aria-label={t('admin.navigationLabel')}>
      {#each modules as module}
        <button class:active={activeModule === module.id} aria-current={activeModule === module.id ? 'page' : undefined} on:click={() => requestModuleChange(module.id)}>
          {t(`admin.modules.${module.key}`)}
        </button>
      {/each}
    </nav>

    <div class="admin-module-content">
      {#if activeModule === 'update'}
        <Update currentVersion={currentVersion} />
      {:else if activeModule === 'password'}
        <AdminPassword {lang} />
      {:else if activeModule === 'backup'}
        <Backup {loadConfig} {lang} />
      {:else if activeModule === 'sensor'}
        <SensorSettings bind:data {loadConfig} {lang} onDirtyStateChange={reportDirty} />
      {:else if activeModule === 'wifi'}
        <WifiSettings {lang} onDirtyStateChange={reportDirty} />
      {:else if activeModule === 'mqtt_ha'}
        <MqttSettings {data} {lang} onDirtyStateChange={reportDirty} />
      {:else if activeModule === 'push'}
        <PushSettings {lang} onDirtyStateChange={reportDirty} />
      {:else}
        <SensorSettings bind:data {loadConfig} {lang} onDirtyStateChange={reportDirty} />
      {/if}
    </div>
  {/if}
</section>

<style>
  .admin-area { display: grid; gap: 16px; }
  .admin-header { display: flex; align-items: flex-end; justify-content: space-between; gap: 18px; padding-bottom: 4px; }
  .admin-header h2 { margin: 4px 0; color: var(--text-main); }
  .admin-header p { margin: 0; color: var(--text-muted); }
  .eyebrow { color: var(--accent); font-size: 0.75rem; font-weight: 800; letter-spacing: 0.12em; text-transform: uppercase; }
  .admin-login-button, .admin-module-nav button { min-height: 40px; border: 1px solid var(--surface-border); border-radius: 8px; background: var(--button-bg); color: var(--button-text); font-weight: 700; cursor: pointer; }
  .admin-login-button { padding: 0 14px; }
  .admin-module-nav { display: flex; flex-wrap: wrap; gap: 8px; padding: 10px; border: 1px solid var(--surface-border); border-radius: 10px; background: var(--surface-2); }
  .admin-module-nav button { padding: 0 12px; }
  .admin-module-nav button.active { border-color: transparent; background: var(--button-active-bg); color: var(--button-active-text); }
  .admin-state { padding: 24px; border: 1px solid var(--surface-border); border-radius: 12px; background: var(--surface-2); color: var(--text-muted); }
  .admin-state--error { border-color: rgba(248, 113, 113, 0.42); color: #fca5a5; }
  @media (max-width: 640px) {
    .admin-header { align-items: flex-start; flex-direction: column; }
    .admin-login-button { width: 100%; }
    .admin-module-nav { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); }
  }
</style>
