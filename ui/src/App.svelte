<script>
  import { onMount } from 'svelte';
  import Dashboard from './Dashboard.svelte';
  import Admin from './Admin.svelte';
  import GlobalDialogs from './GlobalDialogs.svelte';
  import AdminLogin from './AdminLogin.svelte';
  import Update from './Update.svelte';
  import { confirmAction, showNotice } from './dialogStore.js';
  import { adminFetch } from './adminAuth.js';
  import { createTranslator, language, SUPPORTED_LANGS } from './i18n.js';
  import versionData from '../../version.json';

  let versionStr = `${Number(versionData.major) || 0}.${Number(versionData.minor) || 0}.${Number(versionData.commit) || 0}`;  // initial fallback; wird durch API-Wert überschrieben

  let updateAvailable = false;
  let updateUrl = '';
  let latestTag = '';

  let activeTab = 'dashboard';
  let theme = 'night';
  let lang = 'de';
  const DATA_CACHE_KEY = 'level-control-last-data';
  let data = {
    rohdistanz: 0,
    salzstandCm: 0,
    salzstandPercent: 0,
    wifiSignal: 0,
    uptime: 0,
    ip: '',
    ssid: '',
    bssid: '',
    behaelterhoehe: 95,
    offset: 0,
    sampleIntervalSeconds: 5,
    sensorType: 'rcwl1670',
    mqttState: 'unknown',
    sensorValid: false
  };

  let ws;
  let wsConnected = false;
  let lastSensorUpdate = 0;
  let wsDestroyed = false;
  let adminHasUnsavedChanges = false;

  async function requestTabChange(nextTab) {
    if (nextTab === activeTab) {
      return;
    }
    if (activeTab === 'admin' && adminHasUnsavedChanges) {
      const confirmed = await confirmAction({
        title: t('admin.unsavedTitle'),
        message: t('admin.unsavedConfirm'),
        confirmLabel: t('admin.discardChanges'),
        cancelLabel: t('cancel'),
        tone: 'danger'
      });
      if (!confirmed) return;
      adminHasUnsavedChanges = false;
    }
    activeTab = nextTab;
  }

  function restoreCachedData() {
    try {
      const raw = localStorage.getItem(DATA_CACHE_KEY);
      if (!raw) return;
      const cached = JSON.parse(raw);
      data = { ...data, ...cached };
    } catch (_) {
      // Ignore invalid cache and continue with live updates.
    }
  }

  function persistData() {
    try {
      localStorage.setItem(DATA_CACHE_KEY, JSON.stringify(data));
    } catch (_) {
      // Ignore storage write errors (e.g. private mode quota issues).
    }
  }

  function applyTheme(nextTheme) {
    theme = nextTheme;
    document.documentElement.setAttribute('data-theme', theme);
    localStorage.setItem('level-control-theme', theme);
  }

  function setLanguage(nextLang) {
    lang = nextLang;
    localStorage.setItem('stand-language', nextLang);
  }

  function toggleTheme() {
    applyTheme(theme === 'day' ? 'night' : 'day');
  }

  function connectWs() {
    if (wsDestroyed) return;
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${protocol}//${window.location.host}/ws`);

    ws.onopen = () => {
      wsConnected = true;
    };

    ws.onmessage = (event) => {
      const msg = JSON.parse(event.data);
      if (msg.type === 'sensor') {
        data.sensorValid = msg.valid !== false;
        if (data.sensorValid) {
          data.rohdistanz = msg.rohdistanz;
          data.salzstandCm = msg.salzstandCm;
          data.salzstandPercent = msg.salzstandPercent;
          lastSensorUpdate = Date.now();
        }
      } else if (msg.type === 'wifi') {
        data.wifiSignal = msg.signal;
        data.ip = msg.ip;
        data.ssid = msg.ssid;
        data.bssid = msg.bssid;
      } else if (msg.type === 'uptime') {
        data.uptime = msg.uptime;
      } else if (msg.type === 'mqtt') {
        data.mqttState = msg.state;
      }
      persistData();
    };

    ws.onclose = () => {
      wsConnected = false;
      if (!wsDestroyed) {
        setTimeout(connectWs, 3000);
      }
    };

    ws.onerror = () => {
      wsConnected = false;
      ws.close();
    };
  }

  onMount(() => {
    const storedTheme = localStorage.getItem('level-control-theme');
    applyTheme(storedTheme === 'day' ? 'day' : 'night');
    const storedLang = localStorage.getItem('stand-language');
    setLanguage(storedLang === 'en' ? 'en' : 'de');
    restoreCachedData();

    // Connect to WebSocket (with auto-reconnect)
    connectWs();

    // Load initial config
    loadConfig();

    // Zuerst die live installierte Version vom Gerät holen, danach Update-Check
    fetch('/api/update/status', { cache: 'no-store' })
      .then(r => r.json())
      .then(s => { if (s.installedVersion) versionStr = s.installedVersion; })
      .catch(() => {})
      .finally(() => checkForUpdate());

    // Load initial MQTT connection state
    fetch('/api/mqtt/status', { cache: 'no-store' })
      .then(r => r.json())
      .then(s => { data.mqttState = s.state || 'unknown'; })
      .catch(() => {});

    return () => {
      wsDestroyed = true;
      if (ws) ws.close();
    };
  });

  function loadConfig() {
    fetch('/api/config').then(r => r.json()).then(config => {
      data.behaelterhoehe = config.behaelterhoehe;
      data.offset = config.offset;
      data.sampleIntervalSeconds = config.sampleIntervalSeconds || 5;
      data.sensorType = config.sensorType || 'rcwl1670';
      persistData();
    });
  }

  async function checkForUpdate() {
    try {
      const res = await fetch('/api/update/manifest');
      if (!res.ok) return;
      const release = await res.json();
      const clean = (release.version || '').replace(/^v/, '');
      const [rm, rn, rc] = clean.split('.').map(Number);
      const [lm, ln, lc] = versionStr.split('.').map(Number);
      if (rm > lm || (rm === lm && rn > ln) || (rm === lm && rn === ln && rc > lc)) {
        updateAvailable = true;
        updateUrl = release.releaseUrl;
        latestTag = `v${clean}`;
      }
    } catch (_) {
      // Offline oder kein Release – Update-Check wird übersprungen
    }
  }

  async function restartEsp() {
    const t = createTranslator(lang);
    const confirmed = await confirmAction({
      title: t('restartDialogTitle'),
      message: t('restartDialogMessage'),
      confirmLabel: t('restartDialogConfirm'),
      cancelLabel: t('cancel'),
      tone: 'danger'
    });

    if (!confirmed) {
      return;
    }

    try {
      const response = await adminFetch('/api/restart', { method: 'POST' });
      if (!response.ok) {
        showNotice('error', t('restartFailed'));
        return;
      }
      showNotice('success', t('restartSuccess'));
      // Nach dem ESP-Neustart UI automatisch neu laden, damit die Verbindung wieder sauber aufgebaut wird.
      setTimeout(() => {
        window.location.reload();
      }, 6000);
    } catch (_) {
      showNotice('error', t('restartFailed'));
    }
  }

  $: t = createTranslator(lang);
  $: language.set(lang);
  $: tabs = [
    { id: 'dashboard', label: t('modules.dashboard.label') },
    { id: 'admin', label: t('modules.admin.label') }
  ];
</script>

<main>
  <GlobalDialogs />
  <AdminLogin />

  <header class="topbar">
    <div class="topbar-row">
      <div class="brand">
        <span class="brand-logo" aria-hidden="true"></span>
        <h1>{t('appTitle')}</h1>
        <span class="app-version">v{versionStr}</span>
      </div>

      <nav class="tabs" aria-label={t('ui.mainNavigation')}>
        {#each tabs as tab}
          <button class="tab" class:active={activeTab === tab.id} aria-current={activeTab === tab.id ? 'page' : undefined} on:click={() => requestTabChange(tab.id)}>
            <svg viewBox="0 0 24 24" aria-hidden="true">
              {#if tab.id === 'dashboard'}
                <path d="M3 11l9-8 9 8"/><path d="M5 10v10h14V10"/><path d="M10 20v-6h4v6"/>
              {:else}
                <path d="M4 6h8M18 6h2M4 12h2M12 12h8M4 18h10M20 18h0"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>
              {/if}
            </svg>
            {tab.label}
          </button>
        {/each}
      </nav>

      <div class="tools">
        {#if updateAvailable}
          <a class="chip-update" href={updateUrl} target="_blank" rel="noopener noreferrer" title="{t('updateAvailableTitle')}: {latestTag}">
            <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 4v11M7 11l5 5 5-5M4 20h16"/></svg>
            <span>{latestTag}</span>
          </a>
        {/if}
        <button class="icon-btn" on:click={restartEsp} title={t('restart')} aria-label={t('restart')}>
          <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M20 12a8 8 0 1 1-2.6-5.9"/><path d="M20 4v5h-5"/></svg>
        </button>
        <button class="icon-btn" on:click={toggleTheme} title={theme === 'day' ? t('night') : t('day')} aria-label={theme === 'day' ? t('night') : t('day')}>
          {#if theme === 'day'}
            <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M20 14.5A8.5 8.5 0 1 1 9.5 4a7 7 0 0 0 10.5 10.5z"/></svg>
          {:else}
            <svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.5 1.5M17.5 17.5L19 19M5 19l1.5-1.5M17.5 6.5L19 5"/></svg>
          {/if}
        </button>
        <label class="language-pick" aria-label={t('language')}>
          <select bind:value={lang} on:change={(event) => setLanguage(event.currentTarget.value)}>
            {#each SUPPORTED_LANGS as option}
              <option value={option.value}>{option.value.toUpperCase()}</option>
            {/each}
          </select>
        </label>
      </div>
    </div>
  </header>

  <section class="module-shell" class:module-shell--panel={activeTab !== 'dashboard'}>
    {#if activeTab === 'dashboard'}
      <Dashboard bind:data {wsConnected} {lastSensorUpdate} {lang} />
    {:else if activeTab === 'admin'}
      <Admin bind:data {loadConfig} currentVersion={versionStr} {lang} onDirtyStateChange={(dirty) => (adminHasUnsavedChanges = dirty)} />
    {/if}
  </section>

  <footer class="app-footer">
    <p><a href="https://levelcontrol.montag.nrw" target="_blank" rel="noopener noreferrer">levelcontrol.montag.nrw</a> · © 2026 by Marc Montag. All rights reserved.</p>
  </footer>

  <nav class="bottom-nav" aria-label={t('ui.mobileNavigation')}>
    {#each tabs as tab}
      <button class:active={activeTab === tab.id} aria-current={activeTab === tab.id ? 'page' : undefined} on:click={() => requestTabChange(tab.id)}>
        <svg viewBox="0 0 24 24" aria-hidden="true">
          {#if tab.id === 'dashboard'}
            <path d="M3 11l9-8 9 8"/><path d="M5 10v10h14V10"/><path d="M10 20v-6h4v6"/>
          {:else}
            <path d="M4 6h8M18 6h2M4 12h2M12 12h8M4 18h10M20 18h0"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>
          {/if}
        </svg>
        {tab.label}
      </button>
    {/each}
  </nav>
</main>

<style>
  /* Farben: Standard ist Nacht. Die älteren Namen (--text-main, --surface-border ...) bleiben als Aliase,
     damit Admin-Module und Dialoge ohne Umbau mitziehen. */
  :global(:root) {
    --bg: #0a1619;
    --surface: #112327;
    --surface-2: #0d1e21;
    --line: #223a3f;
    --ink: #e4f0f0;
    --muted: #8da6a9;
    --accent: #4fc1ce;
    --accent-ink: #04252a;
    --accent-soft: color-mix(in srgb, var(--accent) 14%, transparent);
    --ok: #4fcb91;
    --warn: #f0b050;
    --bad: #ff7b70;
    --salt: #cdc6b2;
    --salt-dots: #948d78;
    --tank-line: #8da6a9;
    --radius: 11px;

    --bg-main: var(--bg);
    --text-main: var(--ink);
    --text-muted: var(--muted);
    --surface-border: var(--line);
    --button-bg: var(--surface-2);
    --button-text: var(--ink);
    --button-active-bg: var(--accent);
    --button-active-text: var(--accent-ink);
    --card-grad: var(--surface);
    --shadow: none;
  }

  :global(html[data-theme='day']) {
    --bg: #e9efee;
    --surface: #ffffff;
    --surface-2: #f2f6f5;
    --line: #d2dddb;
    --ink: #10262b;
    --muted: #53686c;
    --accent: #0b7a86;
    --accent-ink: #ffffff;
    --ok: #1e8e5a;
    --warn: #a85f00;
    --bad: #c2382f;
    --salt: #f7f3e9;
    --salt-dots: #d2cab4;
    --tank-line: #10262b;
    --shadow: 0 1px 2px rgba(16, 38, 43, 0.06);
  }

  :global(html) {
    color-scheme: dark;
  }

  :global(html[data-theme='day']) {
    color-scheme: light;
  }

  :global(body) {
    margin: 0;
    background: var(--bg);
    color: var(--ink);
    transition: background 0.25s ease, color 0.25s ease;
  }

  :global(button:focus-visible),
  :global(a:focus-visible),
  :global(select:focus-visible) {
    outline: 2px solid var(--accent);
    outline-offset: 2px;
  }

  main {
    font-family: 'Aptos', 'Segoe UI Variable', 'Segoe UI', system-ui, -apple-system, sans-serif;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
  }

  .topbar {
    position: sticky;
    top: 0;
    z-index: 20;
    background: var(--surface);
    border-bottom: 1px solid var(--line);
  }

  .topbar-row {
    display: flex;
    align-items: center;
    gap: 16px;
    max-width: 1100px;
    margin: 0 auto;
    padding: 12px 24px;
  }

  .brand {
    display: flex;
    align-items: center;
    gap: 10px;
    min-width: 0;
  }

  .brand-logo {
    position: relative;
    width: 22px;
    height: 26px;
    border: 2.5px solid var(--accent);
    border-radius: 6px 6px 9px 9px;
    overflow: hidden;
    flex: none;
  }

  .brand-logo::after {
    content: '';
    position: absolute;
    left: 0;
    right: 0;
    bottom: 0;
    height: 55%;
    background: var(--accent);
  }

  h1 {
    margin: 0;
    font-size: 1.05rem;
    font-weight: 700;
    color: var(--ink);
    white-space: nowrap;
  }

  .app-version {
    color: var(--muted);
    font-size: 0.74rem;
    font-variant-numeric: tabular-nums;
  }

  .tabs {
    display: flex;
    gap: 4px;
    margin-inline: auto;
  }

  .tab {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    border: 0;
    border-radius: 999px;
    padding: 8px 15px;
    background: none;
    color: var(--muted);
    font: inherit;
    font-weight: 600;
    cursor: pointer;
  }

  .tab:hover {
    color: var(--ink);
  }

  .tab.active {
    background: var(--ink);
    color: var(--bg);
  }

  svg {
    width: 1.15em;
    height: 1.15em;
    flex: none;
    fill: none;
    stroke: currentColor;
    stroke-width: 1.8;
    stroke-linecap: round;
    stroke-linejoin: round;
  }

  .tools {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .icon-btn {
    display: inline-grid;
    place-items: center;
    width: 34px;
    height: 34px;
    border-radius: var(--radius);
    border: 1px solid var(--line);
    background: var(--surface);
    color: var(--muted);
    cursor: pointer;
  }

  .icon-btn:hover {
    color: var(--ink);
  }

  .chip-update {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 4px 11px;
    border-radius: 999px;
    border: 1px solid color-mix(in srgb, var(--accent) 45%, transparent);
    background: var(--accent-soft);
    color: var(--accent);
    font-size: 0.8rem;
    font-weight: 600;
    text-decoration: none;
    white-space: nowrap;
  }

  .language-pick select {
    height: 34px;
    border-radius: var(--radius);
    border: 1px solid var(--line);
    padding: 0 8px;
    background: var(--surface);
    color: var(--muted);
    font: inherit;
    font-size: 0.8rem;
    font-weight: 700;
    cursor: pointer;
  }

  .module-shell {
    width: 100%;
    max-width: 1100px;
    margin: 0 auto;
    padding: 24px;
    box-sizing: border-box;
    flex: 1;
  }

  /* Admin-Module liegen auf einer Fläche, das Dashboard bringt eigene Karten mit. */
  .module-shell--panel {
    padding: 24px 24px 0;
  }

  .module-shell--panel > :global(*) {
    background: var(--surface);
    border: 1px solid var(--line);
    border-radius: 16px;
    padding: 18px;
    box-sizing: border-box;
  }

  .app-footer {
    max-width: 1100px;
    width: 100%;
    margin: 0 auto;
    padding: 0 24px 20px;
    box-sizing: border-box;
  }

  .app-footer p {
    margin: 0;
    color: var(--muted);
    font-size: 0.74rem;
    text-align: right;
  }

  .app-footer a {
    color: inherit;
    text-underline-offset: 2px;
  }

  .bottom-nav {
    display: none;
  }

  @media (max-width: 720px) {
    .topbar-row {
      padding: 10px 14px;
      gap: 10px;
    }

    .tabs,
    .app-version {
      display: none;
    }

    .tools {
      margin-left: auto;
    }

    .module-shell {
      padding: 14px;
    }

    .module-shell--panel {
      padding: 14px 14px 0;
    }

    .module-shell--panel > :global(*) {
      padding: 12px;
    }

    .app-footer {
      padding: 0 14px 12px;
    }

    .app-footer p {
      text-align: left;
    }

    main {
      padding-bottom: calc(64px + env(safe-area-inset-bottom, 0px));
    }

    .bottom-nav {
      position: fixed;
      left: 0;
      right: 0;
      bottom: 0;
      z-index: 30;
      display: flex;
      gap: 4px;
      padding: 6px 12px calc(6px + env(safe-area-inset-bottom, 0px));
      background: var(--surface);
      border-top: 1px solid var(--line);
    }

    .bottom-nav button {
      flex: 1;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 2px;
      border: 0;
      border-radius: 10px;
      padding: 6px;
      background: none;
      color: var(--muted);
      font: inherit;
      font-size: 0.74rem;
      font-weight: 600;
      cursor: pointer;
    }

    .bottom-nav button svg {
      width: 1.4em;
      height: 1.4em;
    }

    .bottom-nav button.active {
      color: var(--accent);
    }
  }

  @media (prefers-reduced-motion: reduce) {
    :global(*),
    :global(*::before),
    :global(*::after) {
      scroll-behavior: auto !important;
      transition-duration: 0.01ms !important;
      animation-duration: 0.01ms !important;
      animation-iteration-count: 1 !important;
    }
  }
</style>
