<script>
  import { onMount } from 'svelte';
  import { adminFetch } from './adminAuth.js';
  import { createTranslator } from './i18n.js';

  let { data, currentVersion = '', lang = 'de' } = $props();
  const t = $derived(createTranslator(lang));

  let sys = $state(null);
  let failed = $state(false);
  let loading = $state(false);

  const nf = $derived(new Intl.NumberFormat(lang === 'en' ? 'en-US' : 'de-DE', { maximumFractionDigits: 0 }));
  const dateTimeFmt = $derived(new Intl.DateTimeFormat(lang === 'en' ? 'en-GB' : 'de-DE', { dateStyle: 'medium', timeStyle: 'medium' }));
  const kb = (bytes) => `${nf.format((bytes || 0) / 1024)} kB`;
  const percent = (used, total) => (total > 0 ? Math.round((used / total) * 100) : 0);

  function formatUptime(totalSeconds) {
    const days = Math.floor(totalSeconds / 86400);
    const hours = Math.floor((totalSeconds % 86400) / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    return days > 0 ? `${days} d ${hours} h` : `${hours} h ${minutes} m`;
  }

  const rssi = $derived(sys?.rssi ?? data?.wifiSignal ?? 0);
  const rssiLabel = $derived(rssi >= -65 ? 'gut' : rssi >= -80 ? 'mittel' : 'schwach');
  const rssiLabelEn = $derived(rssi >= -65 ? 'good' : rssi >= -80 ? 'fair' : 'weak');

  async function load() {
    loading = true;
    failed = false;
    try {
      const response = await adminFetch('/api/system', { cache: 'no-store' });
      if (!response.ok) throw new Error('http');
      sys = await response.json();
    } catch (_) {
      failed = true;
    } finally {
      loading = false;
    }
  }

  onMount(load);
</script>

<section class="system-card">
  <header>
    <h3>{t('admin.sys.title')}</h3>
    <button class="secondary-sm" type="button" onclick={load} disabled={loading}>{t('admin.sys.refresh')}</button>
  </header>

  {#if failed}
    <p class="sys-error">{t('admin.sys.loadFailed')}</p>
  {:else}
    <dl>
      <div><dt>{t('admin.sys.version')}</dt><dd>{currentVersion}</dd></div>
      {#if sys}<div><dt>{t('admin.sys.chip')}</dt><dd>{sys.chip} · {sys.cpuMhz} MHz</dd></div>{/if}
      <div><dt>{t('admin.sys.uptime')}</dt><dd>{formatUptime(data?.uptime || 0)}</dd></div>
      <div><dt>{t('admin.sys.wifi')}</dt><dd>{rssi} dBm ({lang === 'en' ? rssiLabelEn : rssiLabel})</dd></div>
      {#if sys}
        <div><dt>{t('admin.sys.heap')}</dt><dd>{kb(sys.freeHeap)} {lang === 'en' ? 'of' : 'von'} {kb(sys.heapSize)}</dd></div>
        <div><dt>{t('admin.sys.minHeap')}</dt><dd>{kb(sys.minFreeHeap)}</dd></div>
        <div><dt>{t('admin.sys.flash')}</dt><dd>{kb(sys.sketchSize)} ({percent(sys.sketchSize, sys.flashSize)} % {lang === 'en' ? 'of' : 'von'} {kb(sys.flashSize)})</dd></div>
        <div><dt>{t('admin.sys.fs')}</dt><dd>{kb(sys.fsUsed)} {lang === 'en' ? 'of' : 'von'} {kb(sys.fsTotal)}</dd></div>
        <div><dt>{t('admin.sys.time')}</dt><dd class:bad={!sys.timeValid}>{sys.timeValid ? t('admin.sys.timeOk') : t('admin.sys.timeBad')}</dd></div>
        <div><dt>{t('admin.sys.dateTime')}</dt><dd>{sys.timeValid ? dateTimeFmt.format(sys.deviceEpoch * 1000) : '–'}</dd></div>
      {/if}
      <div><dt>{t('admin.sys.mqtt')}</dt><dd>{data?.mqttState || '–'}</dd></div>
    </dl>
  {/if}
</section>

<style>
  .system-card { display: grid; gap: 12px; padding: 18px; border: 1px solid var(--surface-border); border-radius: 12px; background: var(--surface-2); color: var(--text-main); }
  header { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
  h3 { margin: 0; }
  dl { display: grid; gap: 0; margin: 0; }
  dl div { display: flex; justify-content: space-between; gap: 16px; padding: 9px 0; border-top: 1px solid var(--surface-border); }
  dl div:first-child { border-top: none; }
  dt { color: var(--text-muted); }
  dd { margin: 0; font-weight: 600; text-align: right; }
  dd.bad, .sys-error { color: #fca5a5; }
  button { min-height: 40px; padding: 0 18px; border: 1px solid var(--surface-border); border-radius: 10px; background: transparent; color: var(--text-main); cursor: pointer; font-size: 0.9rem; font-weight: 600; }
</style>
