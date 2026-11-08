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
  let showMobileMoreMenu = false;
  let adminHasUnsavedChanges = false;

  const MOBILE_PRIMARY_TAB_IDS = ['dashboard', 'admin'];

  async function requestTabChange(nextTab) {
    if (nextTab === activeTab) {
      showMobileMoreMenu = false;
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
    showMobileMoreMenu = false;
  }

  function toggleMobileMoreMenu() {
    showMobileMoreMenu = !showMobileMoreMenu;
  }

  function closeMobileMoreMenu() {
    showMobileMoreMenu = false;
  }

  function handleMobileMoreBackdropClick(event) {
    if (event.target === event.currentTarget) {
      closeMobileMoreMenu();
    }
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

  function setTheme(nextTheme) {
    if (nextTheme === theme) return;
    applyTheme(nextTheme);
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
    { id: 'dashboard', label: t('modules.dashboard.label'), subtitle: t('modules.dashboard.subtitle') },
    { id: 'admin', label: t('modules.admin.label'), subtitle: t('modules.admin.subtitle') }
  ];

  $: mobilePrimaryTabs = tabs.filter((tab) => MOBILE_PRIMARY_TAB_IDS.includes(tab.id));
  $: mobileMoreTabs = tabs.filter((tab) => !MOBILE_PRIMARY_TAB_IDS.includes(tab.id));
  $: isMoreTabActive = mobileMoreTabs.some((tab) => tab.id === activeTab);
</script>

<main>
  <GlobalDialogs />
  <AdminLogin />

  {#if showMobileMoreMenu}
    <div class="mobile-more-backdrop" role="presentation" on:click={handleMobileMoreBackdropClick}>
      <div class="mobile-more-sheet" role="dialog" aria-modal="true" aria-label={t('ui.moreModulesAria')}>
        <h3>{t('moreModules')}</h3>
        <div class="mobile-more-list">
          {#each mobileMoreTabs as tab}
            <button class="mobile-more-item" class:active={activeTab === tab.id} aria-current={activeTab === tab.id ? 'page' : undefined} on:click={() => requestTabChange(tab.id)}>
              {tab.label}
            </button>
          {/each}
        </div>
      </div>
    </div>
  {/if}

  <header class="topbar">
    <div class="topbar-row">
      <div class="brand">
        <span class="brand-icon" aria-hidden="true">
          <svg viewBox="0 0 24 24"><path d="M6.25 12a1.75 1.75 0 1 1 3.5 0 1.75 1.75 0 0 1-3.5 0Zm5.2 0a.95.95 0 0 1 .95-.95A5.6 5.6 0 0 0 18 5.45a.95.95 0 1 1 1.9 0 7.5 7.5 0 0 1-7.5 7.5.95.95 0 0 1-.95-.95Zm.95 4.55a.95.95 0 0 1 0-1.9A9.2 9.2 0 0 0 21.6 5.45a.95.95 0 1 1 1.9 0c0 6.04-4.91 10.95-10.95 10.95a.95.95 0 0 1-.15 0Zm0-8.95a.95.95 0 1 1 0-1.9 1.7 1.7 0 0 0 1.7-1.7.95.95 0 1 1 1.9 0 3.6 3.6 0 0 1-3.6 3.6Z"/></svg>
        </span>
        <div class="brand-copy">
          <h1>{t('appTitle')}</h1>
          <p class="brand-line">{t('appTagline')} <span class="app-version">v{versionStr}</span>{#if updateAvailable}<a class="update-badge" href={updateUrl} target="_blank" rel="noopener noreferrer" title="{t('updateAvailableTitle')}: {latestTag}"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-1 13v-4H8l4-4 4 4h-3v4h-2z"/></svg></a>{/if}</p>

        </div>
      </div>
      <div class="top-controls">
        <button class="restart-btn" on:click={restartEsp}>
          <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 6V3L8 7l4 4V8c2.76 0 5 2.24 5 5a5 5 0 0 1-5 5 5 5 0 0 1-4.9-4H5.08A7 7 0 0 0 12 20a7 7 0 0 0 0-14z"/></svg>
          {t('restart')}
        </button>
        <label class="language-pick" aria-label={t('language')}>
          <span>{t('language')}</span>
          <select bind:value={lang} on:change={(event) => setLanguage(event.currentTarget.value)}>
            {#each SUPPORTED_LANGS as option}
              <option value={option.value}>{option.label}</option>
            {/each}
          </select>
        </label>
        <div class="theme-switch" role="group" aria-label={t('ui.themeSelect')}>
          <button class="theme-btn" class:active={theme === 'day'} on:click={() => setTheme('day')}>
            <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3a1 1 0 0 1 1 1v1a1 1 0 1 1-2 0V4a1 1 0 0 1 1-1zm0 15a4 4 0 1 0 0-8 4 4 0 0 0 0 8zm8-5a1 1 0 0 1-1 1h-1a1 1 0 1 1 0-2h1a1 1 0 0 1 1 1zM7 13a1 1 0 0 1-1 1H5a1 1 0 1 1 0-2h1a1 1 0 0 1 1 1zm9.66 5.66a1 1 0 0 1-1.41 0l-.7-.71a1 1 0 0 1 1.41-1.41l.7.7a1 1 0 0 1 0 1.42zM9.45 6.45a1 1 0 0 1-1.41 0l-.71-.7a1 1 0 1 1 1.41-1.42l.71.71a1 1 0 0 1 0 1.41zm7.2-1.66a1 1 0 0 1 0 1.41l-.7.71a1 1 0 0 1-1.42-1.41l.71-.71a1 1 0 0 1 1.41 0zM9.45 17.55a1 1 0 0 1 0 1.41l-.71.7a1 1 0 0 1-1.41-1.41l.7-.71a1 1 0 0 1 1.42 0z"/></svg>
            {t('day')}
          </button>
          <button class="theme-btn" class:active={theme === 'night'} on:click={() => setTheme('night')}>
            <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M20 14.5A8.5 8.5 0 1 1 9.5 4a1 1 0 0 1 1.05 1.47A6.5 6.5 0 1 0 18.53 13.45 1 1 0 0 1 20 14.5z"/></svg>
            {t('night')}
          </button>
        </div>
      </div>
    </div>
  </header>

  <div class="app-layout">
    <aside class="sidebar-nav" aria-label={t('ui.mainNavigation')}>
      {#each tabs as tab}
        <button class="sidebar-item" class:active={activeTab === tab.id} aria-current={activeTab === tab.id ? 'page' : undefined} on:click={() => requestTabChange(tab.id)}>
          {tab.label}
        </button>
      {/each}

      <div class="sidebar-credit-box">
        <a href="https://levelcontrol.montag.nrw" target="_blank" rel="noopener noreferrer">levelcontrol.montag.nrw</a>
      </div>
    </aside>

    <section class="module-shell">
      {#if activeTab === 'dashboard'}
        <Dashboard bind:data {wsConnected} {lastSensorUpdate} {lang} />
      {:else if activeTab === 'admin'}
        <Admin bind:data {loadConfig} currentVersion={versionStr} {lang} onDirtyStateChange={(dirty) => (adminHasUnsavedChanges = dirty)} />
      {/if}
    </section>
  </div>

  <nav class="mobile-nav" aria-label={t('ui.mobileNavigation')}>
    {#each mobilePrimaryTabs as tab}
      <button class="mobile-nav-item" class:active={activeTab === tab.id} aria-current={activeTab === tab.id ? 'page' : undefined} on:click={() => requestTabChange(tab.id)}>
        {tab.label}
      </button>
    {/each}
    <button class="mobile-nav-item mobile-more-trigger" class:active={isMoreTabActive || showMobileMoreMenu} aria-expanded={showMobileMoreMenu} on:click={toggleMobileMoreMenu}>
      {t('more')}
    </button>
  </nav>

  <footer class="app-footer">
    <p>© 2026 by Marc Montag. All rights reserved.</p>
  </footer>
</main>

<style>
  :global(:root) {
    --bg-main: radial-gradient(circle at 20% 0%, #0f2140 0%, #0b1630 46%, #070d1f 100%);
    --text-main: #e8f0ff;
    --text-muted: #a8bddf;
    --surface: rgba(9, 20, 42, 0.72);
    --surface-2: rgba(8, 18, 36, 0.8);
    --surface-border: rgba(112, 146, 198, 0.25);
    --button-bg: #1c345d;
    --button-text: #dceafe;
    --button-active-bg: #2c74bb;
    --button-active-text: #ffffff;
    --accent: #62b8dd;
    --card-grad: linear-gradient(160deg, rgba(20, 38, 74, 0.9) 0%, rgba(12, 27, 56, 0.9) 100%);
    --shadow: 0 12px 30px rgba(0, 6, 20, 0.45);
  }

  :global(html[data-theme='day']) {
    --bg-main: radial-gradient(circle at 20% 0%, #e9f9ec 0%, #dff5e7 45%, #eafcf1 100%);
    --text-main: #183326;
    --text-muted: #3e6a55;
    --surface: rgba(255, 255, 255, 0.86);
    --surface-2: rgba(248, 255, 251, 0.9);
    --surface-border: rgba(80, 146, 110, 0.26);
    --button-bg: #d9efe0;
    --button-text: #1b3d2f;
    --button-active-bg: #3f9a66;
    --button-active-text: #ffffff;
    --accent: #57b67b;
    --card-grad: linear-gradient(160deg, #ffffff 0%, #f2fff7 100%);
    --shadow: 0 12px 26px rgba(53, 104, 73, 0.14);
  }

  :global(html) {
    color-scheme: dark;
  }

  :global(html[data-theme='day']) {
    color-scheme: light;
  }

  :global(body) {
    margin: 0;
    background: var(--bg-main);
    color: var(--text-main);
    transition: background 0.25s ease, color 0.25s ease;
  }

  :global(button:focus-visible),
  :global(a:focus-visible),
  :global(select:focus-visible) {
    outline: 3px solid var(--accent);
    outline-offset: 3px;
  }

  main {
    font-family: 'Aptos', 'Segoe UI Variable', 'Segoe UI', sans-serif;
    max-width: 1300px;
    margin: 0 auto;
    padding: 16px;
  }

  .topbar {
    position: sticky;
    top: 0;
    z-index: 20;
    backdrop-filter: blur(10px);
    background: var(--surface);
    border: 1px solid var(--surface-border);
    border-radius: 16px;
    padding: 14px;
    margin-bottom: 16px;
    box-shadow: var(--shadow);
  }

  .topbar-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin-bottom: 14px;
  }

  h1 {
    margin: 0;
    font-size: clamp(1.35rem, 3vw, 2rem);
    color: var(--text-main);
    letter-spacing: 0.6px;
    text-transform: uppercase;
    font-weight: 900;
    text-shadow: 0 1px 0 rgba(0, 0, 0, 0.12), 0 8px 20px rgba(0, 0, 0, 0.18);
  }

  .brand {
    display: flex;
    align-items: center;
    gap: 10px;
    min-width: 0;
  }

  .brand-icon {
    width: 40px;
    height: 40px;
    border-radius: 12px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    background: linear-gradient(150deg, rgba(98, 184, 221, 0.2) 0%, rgba(98, 184, 221, 0.05) 100%);
    border: 1px solid var(--surface-border);
    color: var(--accent);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.08);
  }

  .brand-icon svg {
    width: 20px;
    height: 20px;
    fill: currentColor;
  }

  .brand-copy {
    min-width: 0;
  }

  .brand-line {
    margin: 3px 0 0;
    color: var(--text-muted);
    font-size: 0.8rem;
    letter-spacing: 0.16em;
    text-transform: uppercase;
  }

  .sidebar-credit-box {
    background: var(--surface);
    border: 1px solid var(--surface-border);
    border-radius: 10px;
    padding: 10px 12px;
    box-shadow: var(--shadow);
    font-size: 0.68rem;
    letter-spacing: 0.04em;
    text-align: center;
    opacity: 0.6;
  }

  .sidebar-credit-box a {
    color: var(--text-muted);
    text-decoration: none;
    text-underline-offset: 2px;
  }

  .sidebar-credit-box a:hover {
    text-decoration: underline;
    opacity: 1;
  }

  .app-version {
    opacity: 0.55;
    font-size: 0.72rem;
    letter-spacing: 0.08em;
    margin-left: 6px;
    font-variant-numeric: tabular-nums;
  }

  .update-badge {
    display: inline-flex;
    align-items: center;
    margin-left: 5px;
    color: #f59e0b;
    vertical-align: middle;
    text-decoration: none;
    transition: transform 0.15s, color 0.15s;
  }

  .update-badge:hover {
    transform: scale(1.2);
    color: #d97706;
  }

  .update-badge svg {
    width: 14px;
    height: 14px;
    fill: currentColor;
  }

  .theme-switch {
    display: inline-flex;
    gap: 6px;
    padding: 4px;
    border: 1px solid var(--surface-border);
    border-radius: 999px;
    background: var(--surface-2);
  }

  .top-controls {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;
  }

  .language-pick {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    color: var(--text-muted);
    font-size: 0.8rem;
    font-weight: 700;
  }

  .language-pick select {
    min-height: 34px;
    border-radius: 999px;
    border: 1px solid var(--surface-border);
    padding: 0 12px;
    background: var(--surface-2);
    color: var(--text-main);
    font-weight: 700;
  }

  .restart-btn {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 6px 10px;
    border-radius: 999px;
    border: 1px solid var(--surface-border);
    background: var(--button-bg);
    color: var(--button-text);
    font-size: 0.86rem;
    font-weight: 700;
    cursor: pointer;
  }

  .restart-btn svg {
    width: 15px;
    height: 15px;
    fill: currentColor;
  }

  .restart-btn:hover {
    background: var(--button-active-bg);
    color: var(--button-active-text);
  }

  .theme-btn {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 6px 10px;
    border-radius: 999px;
    border: none;
    background: transparent;
    color: var(--text-muted);
    font-size: 0.86rem;
    font-weight: 700;
    cursor: pointer;
  }

  .theme-btn svg {
    width: 15px;
    height: 15px;
    fill: currentColor;
  }

  .theme-btn.active {
    background: var(--button-active-bg);
    color: var(--button-active-text);
    box-shadow: 0 6px 14px rgba(0, 0, 0, 0.18);
  }

  .app-layout {
    display: grid;
    grid-template-columns: 240px minmax(0, 1fr);
    grid-template-areas: "sidebar content";
    gap: 14px;
    align-items: start;
  }

  .sidebar-nav {
    grid-area: sidebar;
  }

  .module-shell {
    grid-area: content;
  }

  .sidebar-nav {
    position: sticky;
    top: 108px;
    display: grid;
    gap: 8px;
    background: var(--surface);
    border: 1px solid var(--surface-border);
    border-radius: 10px;
    padding: 10px;
    box-shadow: var(--shadow);
  }

  .sidebar-credit-box {
    margin-top: 8px;
  }

  .sidebar-item {
    border: 1px solid transparent;
    border-radius: 8px;
    padding: 10px 12px;
    background: transparent;
    color: var(--text-muted);
    text-align: left;
    font-weight: 700;
    letter-spacing: 0.02em;
    cursor: pointer;
    transition: background 0.2s ease, border-color 0.2s ease, color 0.2s ease;
  }

  .sidebar-item:hover {
    background: rgba(255, 255, 255, 0.06);
    border-color: var(--surface-border);
    color: var(--text-main);
  }

  .sidebar-item.active {
    background: rgba(98, 184, 221, 0.16);
    border-color: rgba(98, 184, 221, 0.42);
    color: var(--text-main);
  }

  :global(html[data-theme='day']) .sidebar-item:hover {
    background: rgba(63, 154, 102, 0.08);
  }

  :global(html[data-theme='day']) .sidebar-item.active {
    background: rgba(63, 154, 102, 0.14);
    border-color: rgba(63, 154, 102, 0.35);
  }

  .mobile-nav {
    display: none;
  }

  .mobile-more-backdrop {
    position: fixed;
    inset: 0;
    z-index: 90;
    background: rgba(3, 10, 22, 0.5);
    display: flex;
    align-items: flex-end;
    justify-content: center;
    padding: 10px;
  }

  .mobile-more-sheet {
    width: min(480px, 100%);
    border: 1px solid var(--surface-border);
    border-radius: 12px;
    background: var(--card-grad);
    box-shadow: var(--shadow);
    padding: 12px;
  }

  .mobile-more-sheet h3 {
    margin: 2px 0 10px;
    font-size: 0.96rem;
    letter-spacing: 0.04em;
    text-transform: uppercase;
    color: var(--text-muted);
  }

  .mobile-more-list {
    display: grid;
    gap: 6px;
  }

  .mobile-more-item {
    border: 1px solid var(--surface-border);
    border-radius: 8px;
    background: transparent;
    color: var(--text-main);
    font-weight: 700;
    letter-spacing: 0.01em;
    text-align: left;
    padding: 10px 12px;
    cursor: pointer;
  }

  .mobile-more-item.active {
    background: rgba(98, 184, 221, 0.18);
    border-color: rgba(98, 184, 221, 0.42);
  }

  .module-shell {
    background: var(--surface);
    border: 1px solid var(--surface-border);
    border-radius: 16px;
    padding: 18px;
    box-shadow: var(--shadow);
    min-height: 340px;
  }

  .app-footer {
    margin-top: 14px;
    padding: 0 2px;
  }

  .app-footer p {
    margin: 0;
    color: var(--text-muted);
    font-size: 0.74rem;
    letter-spacing: 0.05em;
    text-align: right;
    opacity: 0.82;
  }

  @media (max-width: 900px) {
    main {
      padding: 10px;
      padding-bottom: 86px;
    }

    .app-layout {
      display: block;
    }

    .sidebar-nav {
      display: none;
    }

    .module-shell {
      padding: 12px;
    }

    .mobile-nav {
      position: fixed;
      left: 10px;
      right: 10px;
      bottom: max(10px, env(safe-area-inset-bottom));
      z-index: 95;
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 6px;
      border: 1px solid var(--surface-border);
      border-radius: 10px;
      background: var(--surface-2);
      backdrop-filter: blur(10px);
      box-shadow: var(--shadow);
      padding: 6px;
    }

    .mobile-nav {
      padding-bottom: max(6px, env(safe-area-inset-bottom));
    }

    .topbar-row {
      align-items: flex-start;
      flex-direction: column;
      margin-bottom: 12px;
    }

    .mobile-nav-item {
      border: 1px solid transparent;
      border-radius: 8px;
      background: transparent;
      color: var(--text-muted);
      font-size: 0.78rem;
      font-weight: 700;
      letter-spacing: 0.02em;
      padding: 10px 6px;
      text-align: center;
      cursor: pointer;
    }

    .mobile-nav-item.active {
      border-color: rgba(98, 184, 221, 0.42);
      background: rgba(98, 184, 221, 0.16);
      color: var(--text-main);
    }

    .app-footer {
      margin-top: 12px;
      padding-bottom: 6px;
    }

    .app-footer p {
      text-align: left;
      font-size: 0.7rem;
    }

    .mobile-more-trigger {
      color: var(--button-text);
    }
  }

  @media (max-width: 420px) {
    .mobile-nav-item {
      font-size: 0.72rem;
      padding: 9px 4px;
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
