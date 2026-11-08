<script>
  import { onMount } from 'svelte';
  import { createTranslator } from './i18n.js';
  import { showNotice } from './dialogStore.js';
  import { adminFetch } from './adminAuth.js';

  export let data;
  export let wsConnected = true;
  export let lastSensorUpdate = 0;
  export let lang = 'de';

  const PERIOD_OPTIONS = [
    { id: '1m', key: 'period1m', months: 1 },
    { id: '3m', key: 'period3m', months: 3 },
    { id: '6m', key: 'period6m', months: 6 },
    { id: '12m', key: 'period12m', months: 12 }
  ];

  const CHART_WIDTH = 920;
  const CHART_HEIGHT = 220;
  const CHART_PADDING = { top: 18, right: 18, bottom: 34, left: 18 };

  let activePeriod = '3m';
  let showResetConfirm = false;
  let history = [];  // { ts: Unix-ms, value: Prozent }
  let historyLoaded = false;

  function formatUptime(totalSeconds) {
    const hours = Math.floor(totalSeconds / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    return `${hours}h ${minutes}m`;
  }

  function formatDateLabel(timestamp) {
    return new Intl.DateTimeFormat(lang === 'en' ? 'en-US' : 'de-DE', {
      day: '2-digit',
      month: '2-digit',
      year: '2-digit'
    }).format(timestamp);
  }

  function formatDateTimeLabel(timestamp) {
    return new Intl.DateTimeFormat(lang === 'en' ? 'en-US' : 'de-DE', {
      day: '2-digit',
      month: '2-digit',
      year: '2-digit',
      hour: '2-digit',
      minute: '2-digit'
    }).format(timestamp);
  }

  function getPeriodStart(periodId) {
    const option = PERIOD_OPTIONS.find((entry) => entry.id === periodId) || PERIOD_OPTIONS[1];
    const start = new Date();
    start.setMonth(start.getMonth() - option.months);
    return start.getTime();
  }

  async function fetchHistory() {
    try {
      const res = await fetch('/api/history');
      if (!res.ok) return;
      const raw = await res.json();
      // ESP liefert Sekunden (Unix), intern arbeiten wir mit Millisekunden
      history = Array.isArray(raw)
        ? raw
            .filter((e) => Number.isFinite(e?.ts) && Number.isFinite(e?.v))
            .map((e) => ({ ts: e.ts * 1000, value: e.v }))
        : [];
    } catch (_) {
      history = [];
    } finally {
      historyLoaded = true;
    }
  }

  async function resetHistory() {
    showResetConfirm = false;
    try {
      const response = await adminFetch('/api/history', { method: 'DELETE' });
      if (!response.ok) {
        throw new Error(t('dashboard.clearFailed'));
      }
      history = [];
    } catch (error) {
      showNotice('error', error?.message || t('dashboard.clearFailed'));
    }
  }

  function buildPath(points, minValue, maxValue) {
    if (points.length === 0) {
      return '';
    }

    const innerWidth = CHART_WIDTH - CHART_PADDING.left - CHART_PADDING.right;
    const innerHeight = CHART_HEIGHT - CHART_PADDING.top - CHART_PADDING.bottom;
    const startTs = points[0].ts;
    const endTs = points[points.length - 1].ts;
    const timeRange = Math.max(endTs - startTs, 1);
    const valueRange = Math.max(maxValue - minValue, 1);

    return points.map((point, index) => {
      const x = CHART_PADDING.left + ((point.ts - startTs) / timeRange) * innerWidth;
      const y = CHART_PADDING.top + innerHeight - ((point.value - minValue) / valueRange) * innerHeight;
      return `${index === 0 ? 'M' : 'L'} ${x.toFixed(2)} ${y.toFixed(2)}`;
    }).join(' ');
  }

  function buildAreaPath(points, linePath) {
    if (points.length === 0 || !linePath) {
      return '';
    }

    const innerHeight = CHART_HEIGHT - CHART_PADDING.bottom;
    const firstX = CHART_PADDING.left;
    const lastX = CHART_WIDTH - CHART_PADDING.right;
    return `${linePath} L ${lastX.toFixed(2)} ${innerHeight.toFixed(2)} L ${firstX.toFixed(2)} ${innerHeight.toFixed(2)} Z`;
  }

  let nowMs = Date.now();
  let staleCheckInterval;
  $: t = createTranslator(lang);

  onMount(() => {
    fetchHistory();
    staleCheckInterval = setInterval(() => { nowMs = Date.now(); }, 1000);
    return () => clearInterval(staleCheckInterval);
  });

  $: staleThresholdMs = Math.max((data.sampleIntervalSeconds || 5) * 1000 * 2, 30000);
  $: isStale = lastSensorUpdate > 0 && (nowMs - lastSensorUpdate) > staleThresholdMs;
  $: lastUpdateAgeSec = lastSensorUpdate > 0 ? Math.floor((nowMs - lastSensorUpdate) / 1000) : null;

  $: periodStart = getPeriodStart(activePeriod);
  $: filteredHistory = history.filter((entry) => entry.ts >= periodStart);
  $: chartPoints = filteredHistory;
  $: currentPercent = Number.isFinite(data?.salzstandPercent) ? data.salzstandPercent : 0;
  $: sensorState = !wsConnected
    ? 'disconnected'
    : data.sensorValid === false
      ? 'error'
      : isStale
        ? 'stale'
        : currentPercent <= 20
          ? 'refill'
          : 'ok';
  $: sensorStateLabel = t(`dashboard.sensorState${sensorState.charAt(0).toUpperCase()}${sensorState.slice(1)}`);
  $: chartValues = chartPoints.map((entry) => entry.value);
  $: chartReferenceValues = data.sensorValid !== false ? [...chartValues, currentPercent] : chartValues;
  $: valueFloor = chartReferenceValues.length > 0 ? Math.min(...chartReferenceValues) : 0;
  $: valueCeil = chartReferenceValues.length > 0 ? Math.max(...chartReferenceValues) : 100;
  $: chartMin = Math.max(0, Math.floor((valueFloor - 5) / 5) * 5);
  $: chartMax = Math.min(100, Math.ceil((valueCeil + 5) / 5) * 5 || 100);
  $: linePath = buildPath(chartPoints, chartMin, chartMax);
  $: areaPath = buildAreaPath(chartPoints, linePath);
  $: latestPoint = chartPoints[chartPoints.length - 1] || null;
  $: oldestPoint = chartPoints[0] || null;
  $: wifiQuality = data.wifiSignal >= -65 ? 'good' : data.wifiSignal >= -80 ? 'medium' : 'bad';
</script>

<div class="grid">
  <div class="card card-chart">
    <div class="chart-head">
      <div class="chart-head-main">
        <h2><span class="mini-icon"><svg viewBox="0 0 24 24"><path d="M4 18h16v2H4v-2zm2-3.5 3.5-3.5 2.5 2.5L18 8l1.4 1.4-6.9 6.9-2.5-2.5L7.4 16z"/></svg></span>{t('dashboard.historyTitle')}</h2>
        <button class="btn-reset" on:click={() => (showResetConfirm = true)} title={t('dashboard.resetHistory')}>
          <svg viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
          {t('dashboard.resetHistory')}
        </button>
      </div>
    </div>

    {#if showResetConfirm}
      <div class="reset-confirm" role="alert">
        <p>{t('dashboard.clearConfirm')}<br><small>{t('dashboard.clearNote')}</small></p>
        <div class="reset-confirm-actions">
          <button class="btn-cancel" on:click={() => (showResetConfirm = false)}>{t('dashboard.cancel')}</button>
          <button class="btn-danger" on:click={resetHistory}>{t('dashboard.clearYes')}</button>
        </div>
      </div>
    {/if}

    <div class="top-stats">
      <div class="top-stat-card">
        <h3><span class="mini-icon"><svg viewBox="0 0 24 24"><path d="M3 17h18v2H3v-2zm2-8h14v2H5V9zm3-6h8v2H8V3z"/></svg></span>{t('dashboard.distance')}</h3>
        <p class="value">{data.rohdistanz.toFixed(2)} m</p>
      </div>
      <div class="top-stat-card">
        <h3><span class="mini-icon"><svg viewBox="0 0 24 24"><path d="M5 20h14V4H5v16zm2-2v-4h10v4H7zm0-6V6h10v6H7z"/></svg></span>{t('dashboard.level')}</h3>
        <p class="value">{data.salzstandCm.toFixed(1)} cm <span class="value-equiv">≙</span> {data.salzstandPercent.toFixed(1)} %</p>
      </div>
    </div>
    <div class="sensor-state" class:warning={sensorState === 'refill' || sensorState === 'stale'} class:error={sensorState === 'error' || sensorState === 'disconnected'}>
      <span>{t('dashboard.sensorStateLabel')}</span>
      <strong>{sensorStateLabel}</strong>
    </div>

    {#if !wsConnected}
      <div class="ws-alert ws-alert--error" role="alert">
        <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M1 21h22L12 2 1 21zm12-3h-2v-2h2v2zm0-4h-2v-4h2v4z"/></svg>
        {t('dashboard.disconnected')}{lastUpdateAgeSec !== null ? ` (${lang === 'en' ? `${lastUpdateAgeSec}s ago` : `vor ${lastUpdateAgeSec} s`})` : ''}</div>
    {:else if isStale}
      <div class="ws-alert ws-alert--warn" role="alert">
        <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-2h2v2zm0-4h-2V7h2v6z"/></svg>
        {t('dashboard.stale')} ({lang === 'en' ? `${lastUpdateAgeSec}s ago` : `vor ${lastUpdateAgeSec} s`} - {lang === 'en' ? 'no new update received' : 'kein neues Update empfangen'})</div>
    {:else if sensorState === 'error'}
      <div class="ws-alert ws-alert--error" role="alert">{t('dashboard.sensorError')}</div>
    {:else if sensorState === 'refill'}
      <div class="ws-alert ws-alert--warn" role="status">{t('dashboard.refill')}</div>
    {/if}

    <div class="chart-meta">
      <div>
        <span>{t('dashboard.period')}</span>
        <div class="chart-periods" role="group" aria-label={t('dashboard.periodAria')}>
          {#each PERIOD_OPTIONS as option}
            <button class:active={activePeriod === option.id} on:click={() => activePeriod = option.id}>{t('dashboard.' + option.key)}</button>
          {/each}
        </div>
      </div>
      <div>
        <span>{t('dashboard.recordStart')}</span>
        <strong>{oldestPoint ? formatDateLabel(oldestPoint.ts) : t('dashboard.noHistory')}</strong>
      </div>
      <div>
        <span>{t('dashboard.lastHistoryPoint')}</span>
        <strong>{latestPoint ? formatDateTimeLabel(latestPoint.ts) : t('dashboard.noHistory')}</strong>
      </div>
    </div>

    <div class="chart-shell">
      <svg viewBox={`0 0 ${CHART_WIDTH} ${CHART_HEIGHT}`} class="chart" role="img" aria-label={t('dashboard.chartAria')}>
        <defs>
          <linearGradient id="level-control-area" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stop-color="var(--accent)" stop-opacity="0.32" />
            <stop offset="100%" stop-color="var(--accent)" stop-opacity="0.02" />
          </linearGradient>
        </defs>

        <line class="chart-grid" x1={CHART_PADDING.left} y1={CHART_PADDING.top} x2={CHART_WIDTH - CHART_PADDING.right} y2={CHART_PADDING.top} />
        <line class="chart-grid" x1={CHART_PADDING.left} y1={(CHART_HEIGHT - CHART_PADDING.bottom + CHART_PADDING.top) / 2} x2={CHART_WIDTH - CHART_PADDING.right} y2={(CHART_HEIGHT - CHART_PADDING.bottom + CHART_PADDING.top) / 2} />
        <line class="chart-grid" x1={CHART_PADDING.left} y1={CHART_HEIGHT - CHART_PADDING.bottom} x2={CHART_WIDTH - CHART_PADDING.right} y2={CHART_HEIGHT - CHART_PADDING.bottom} />

        {#if areaPath}
          <path d={areaPath} class="chart-area" />
          <path d={linePath} class="chart-line" />
        {/if}

        {#if latestPoint}
          <circle
            class="chart-dot"
            cx={CHART_WIDTH - CHART_PADDING.right}
            cy={CHART_PADDING.top + (CHART_HEIGHT - CHART_PADDING.top - CHART_PADDING.bottom) - (((latestPoint.value - chartMin) / Math.max(chartMax - chartMin, 1)) * (CHART_HEIGHT - CHART_PADDING.top - CHART_PADDING.bottom))}
            r="5"
          />
        {/if}
      </svg>

      <div class="chart-axis chart-axis-top">{chartMax.toFixed(0)} %</div>
      <div class="chart-axis chart-axis-middle">{((chartMax + chartMin) / 2).toFixed(0)} %</div>
      <div class="chart-axis chart-axis-bottom">{chartMin.toFixed(0)} %</div>
    </div>
  </div>

  <div class="card card-combined card-under-chart">
    <h2><span class="mini-icon"><svg viewBox="0 0 24 24"><path d="M12 18a2 2 0 1 0 0 4 2 2 0 0 0 0-4zm0-4c2.56 0 4.92 1.04 6.62 2.73l1.42-1.41A11.96 11.96 0 0 0 12 12c-3.12 0-5.96 1.19-8.04 3.14l1.42 1.41A9.33 9.33 0 0 1 12 14z"/></svg></span>{t('dashboard.system')}</h2>
    <div class="combined-stats">
      <div>
        <span>{t('dashboard.wifiSignal')}</span>
        <div class="wifi-signal-row">
          <p class="value">{data.wifiSignal} dBm</p>
          <span class="wifi-dot wifi-dot--{wifiQuality}" title="{wifiQuality === 'good' ? t('dashboard.qualityGood') : wifiQuality === 'medium' ? t('dashboard.qualityMedium') : t('dashboard.qualityBad')}"></span>
        </div>
      </div>
      <div>
        <span>{t('dashboard.uptime')}</span>
        <p class="value">{formatUptime(data.uptime)}</p>
      </div>
    </div>
  </div>
  <div class="card card-under-chart">
    <h2><span class="mini-icon"><svg viewBox="0 0 24 24"><path d="M12 2 2 7l10 5 8-4v6h2V7L12 2zm-8 9v6l8 4 8-4v-6l-8 4-8-4z"/></svg></span>{t('dashboard.network')}</h2>
    <div class="combined-stats">
      <div>
        <span>IP</span>
        <p class="value value-sm">{data.ip}</p>
      </div>
      <div>
        <span>SSID</span>
        <p class="value value-sm">{data.ssid}</p>
      </div>
    </div>
  </div>
</div>

<style>
  h2 {
    margin: 4px 0;
    font-size: clamp(1.2rem, 2.6vw, 1.8rem);
    color: transparent;
    background: linear-gradient(100deg, var(--text-main) 0%, var(--accent) 100%);
    -webkit-background-clip: text;
    background-clip: text;
  }

  .grid {
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 14px;
  }

  .card {
    border: 1px solid var(--surface-border);
    padding: 16px;
    border-radius: 14px;
    background: var(--card-grad);
    box-shadow: var(--shadow);
  }

  .card h2 {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 1rem;
    margin: 0 0 8px 0;
    color: var(--text-muted);
  }

  .mini-icon {
    width: 24px;
    height: 24px;
    border-radius: 8px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border: 1px solid var(--surface-border);
    background: rgba(255, 255, 255, 0.08);
    color: var(--accent);
  }

  .mini-icon svg {
    width: 14px;
    height: 14px;
    fill: currentColor;
  }

  .card p {
    margin: 4px 0;
    font-weight: 600;
    color: var(--text-main);
  }

  .card p.value {
    font-size: clamp(1.35rem, 3.2vw, 2.05rem);
    font-weight: 800;
    letter-spacing: 0.02em;
    line-height: 1.1;
  }

  .card-chart {
    grid-column: 1 / -1;
  }

  .chart-head {
    display: grid;
    gap: 10px;
    margin-bottom: 14px;
  }

  .chart-head-main {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 14px;
  }

  .chart-head-main h2 {
    margin: 0;
  }

  .chart-periods {
    display: inline-flex;
    flex-wrap: wrap;
    justify-content: flex-start;
    gap: 8px;
  }

  .top-stats {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 12px;
    margin-bottom: 12px;
  }

  .top-stat-card {
    padding: 12px 14px;
    border-radius: 12px;
    border: 1px solid var(--surface-border);
    background: rgba(255, 255, 255, 0.03);
  }

  .top-stat-card h3 {
    display: flex;
    align-items: center;
    gap: 8px;
    margin: 0 0 8px 0;
    font-size: 1rem;
    color: var(--text-muted);
  }

  .top-stat-card p {
    margin: 4px 0;
    font-weight: 600;
    color: var(--text-main);
  }

  .top-stat-card p.value {
    font-size: clamp(1.35rem, 3.2vw, 2.05rem);
    font-weight: 800;
    letter-spacing: 0.02em;
    line-height: 1.1;
  }

  .sensor-state {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin: 0 0 12px;
    padding: 10px 12px;
    border: 1px solid rgba(74, 222, 128, 0.28);
    border-radius: 10px;
    background: rgba(74, 222, 128, 0.08);
    color: var(--text-main);
  }

  .sensor-state span {
    color: var(--text-muted);
    font-size: 0.8rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.08em;
  }

  .sensor-state.warning {
    border-color: rgba(245, 158, 11, 0.36);
    background: rgba(245, 158, 11, 0.1);
  }

  .sensor-state.error {
    border-color: rgba(248, 113, 113, 0.4);
    background: rgba(248, 113, 113, 0.1);
  }

  .btn-reset {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    border: 1px solid rgba(255, 90, 90, 0.3);
    background: transparent;
    color: rgba(255, 100, 100, 0.7);
    padding: 6px 10px;
    border-radius: 8px;
    font-size: 0.78rem;
    font-weight: 700;
    cursor: pointer;
    transition: background 0.15s, color 0.15s;
  }

  .btn-reset:hover {
    background: rgba(255, 80, 80, 0.15);
    color: rgb(255, 110, 110);
  }

  .btn-reset svg {
    width: 13px;
    height: 13px;
    fill: currentColor;
  }

  .reset-confirm {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
    margin-bottom: 14px;
    padding: 12px 16px;
    border-radius: 12px;
    border: 1px solid rgba(255, 80, 80, 0.4);
    background: rgba(255, 60, 60, 0.08);
  }

  .reset-confirm p {
    margin: 0;
    font-size: 0.9rem;
    color: var(--text-main);
    font-weight: 600;
  }

  .reset-confirm small {
    font-weight: 500;
    color: var(--text-muted);
  }

  .reset-confirm-actions {
    display: flex;
    gap: 8px;
    flex-shrink: 0;
  }

  .btn-danger {
    border: none;
    background: rgba(210, 45, 45, 0.85);
    color: #fff;
    padding: 8px 14px;
    border-radius: 8px;
    font-weight: 700;
    cursor: pointer;
  }

  .btn-cancel {
    border: 1px solid var(--surface-border);
    background: rgba(255, 255, 255, 0.05);
    color: var(--text-muted);
    padding: 8px 14px;
    border-radius: 8px;
    font-weight: 700;
    cursor: pointer;
  }

  .chart-periods button {
    border: 1px solid var(--surface-border);
    background: rgba(255, 255, 255, 0.05);
    color: var(--text-muted);
    padding: 8px 12px;
    border-radius: 999px;
    font-weight: 700;
    cursor: pointer;
  }

  .chart-periods button.active {
    background: var(--button-active-bg);
    color: var(--button-active-text);
    border-color: transparent;
  }

  .chart-meta {
    display: grid;
    grid-template-columns: repeat(3, minmax(0, 1fr));
    gap: 12px;
    margin-bottom: 16px;
  }

  .chart-meta div {
    padding: 12px 14px;
    border-radius: 12px;
    border: 1px solid var(--surface-border);
    background: rgba(255, 255, 255, 0.03);
  }

  .chart-meta span,
  .combined-stats span {
    display: block;
    color: var(--text-muted);
    font-size: 0.8rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    margin-bottom: 6px;
  }

  .chart-meta strong {
    font-size: 1rem;
  }

  .chart-shell {
    position: relative;
    border-radius: 16px;
    border: 1px solid var(--surface-border);
    background: linear-gradient(180deg, rgba(255, 255, 255, 0.06) 0%, rgba(255, 255, 255, 0.02) 100%);
    padding: 14px 14px 22px 54px;
    overflow: hidden;
  }

  .chart {
    width: 100%;
    display: block;
  }

  .chart-grid {
    stroke: rgba(255, 255, 255, 0.12);
    stroke-width: 1;
  }

  .chart-line {
    fill: none;
    stroke: var(--accent);
    stroke-width: 4;
    stroke-linecap: round;
    stroke-linejoin: round;
  }

  .chart-area {
    fill: url(#level-control-area);
  }

  .chart-dot {
    fill: var(--accent);
    stroke: rgba(255, 255, 255, 0.78);
    stroke-width: 2;
  }

  .chart-axis {
    position: absolute;
    left: 16px;
    color: var(--text-muted);
    font-size: 0.76rem;
    font-weight: 700;
  }

  .chart-axis-top {
    top: 12px;
  }

  .chart-axis-middle {
    top: calc(50% - 10px);
  }

  .chart-axis-bottom {
    bottom: 14px;
  }

  .combined-stats {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 14px;
  }

  .combined-stats > div {
    padding: 12px 14px;
    border-radius: 12px;
    border: 1px solid var(--surface-border);
    background: rgba(255, 255, 255, 0.03);
  }

  .wifi-signal-row {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .wifi-dot {
    width: 11px;
    height: 11px;
    border-radius: 50%;
    flex-shrink: 0;
    margin-bottom: 2px;
  }

  .wifi-dot--good  { background: #4caf50; box-shadow: 0 0 6px rgba(76,175,80,0.55); }
  .wifi-dot--medium { background: #ff9800; box-shadow: 0 0 6px rgba(255,152,0,0.55); }
  .wifi-dot--bad   { background: #f44336; box-shadow: 0 0 6px rgba(244,67,54,0.55); }

  .value-equiv {
    font-size: 0.65em;
    opacity: 0.6;
    margin: 0 3px;
    font-weight: 600;
  }

  .value-sm {
    font-size: 1rem !important;
    font-weight: 700 !important;
    letter-spacing: 0 !important;
  }

  .chart-meta .chart-periods {
    margin-top: 4px;
    flex-wrap: wrap;
  }

  .chart-meta .chart-periods button {
    padding: 5px 9px;
    font-size: 0.75rem;
  }

  .card-under-chart {
    grid-column: span 2;
  }

  .ws-alert {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 9px 14px;
    border-radius: 10px;
    font-size: 0.82rem;
    font-weight: 700;
    margin-bottom: 12px;
  }

  .ws-alert svg {
    width: 16px;
    height: 16px;
    fill: currentColor;
    flex-shrink: 0;
  }

  .ws-alert--error {
    background: rgba(220, 50, 50, 0.14);
    border: 1px solid rgba(220, 50, 50, 0.45);
    color: #f07070;
  }

  .ws-alert--warn {
    background: rgba(220, 160, 30, 0.14);
    border: 1px solid rgba(220, 160, 30, 0.45);
    color: #d4a640;
  }

  :global(html[data-theme='day']) .ws-alert--error {
    background: rgba(200, 30, 30, 0.08);
    color: #b52222;
  }

  :global(html[data-theme='day']) .ws-alert--warn {
    background: rgba(180, 110, 0, 0.08);
    color: #8a5c00;
  }

  @media (max-width: 980px) {
    .grid {
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }

    .chart-meta {
      grid-template-columns: 1fr;
    }

    .chart-periods {
      justify-content: flex-start;
    }

    .top-stats {
      grid-template-columns: 1fr;
    }

    .card-under-chart {
      grid-column: span 1;
    }
  }

  @media (max-width: 640px) {
    .grid {
      grid-template-columns: 1fr;
    }

    .chart-shell {
      padding: 14px 10px 18px 46px;
    }

    .card-combined .combined-stats,
    .chart-meta {
      grid-template-columns: 1fr;
    }
  }
</style>

