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
    { id: '1m', label: '1 M', months: 1 },
    { id: '3m', label: '3 M', months: 3 },
    { id: '6m', label: '6 M', months: 6 },
    { id: '12m', label: '12 M', months: 12 }
  ];

  const REFILL_THRESHOLD = 20;   // Prozent, darunter gilt „Nachfüllen"
  const DAY_MS = 86400000;
  const CHART_HEIGHT = 250;
  const Y_GRID = [0, 25, 50, 75, 100];

  let activePeriod = '3m';
  let showResetConfirm = false;
  let history = [];  // { ts: Unix-ms, value: Prozent }, aufsteigend nach Zeit
  let chartWidth = 0;
  let hover = null;  // { point, x, y } unter dem Mauszeiger

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
            .sort((a, b) => a.ts - b.ts)
        : [];
    } catch (_) {
      history = [];
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

  function formatUptime(totalSeconds) {
    const days = Math.floor(totalSeconds / 86400);
    const hours = Math.floor((totalSeconds % 86400) / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    return days > 0 ? `${days} d ${hours} h` : `${hours} h ${minutes} m`;
  }

  // Verbrauch aus den letzten 21 Tagen seit der letzten Nachfüllung (Sprung > 40 Prozentpunkte) -> Tage bis zur Schwelle.
  function computeReach(points, percent) {
    if (points.length < 2 || !Number.isFinite(percent)) return null;
    const end = points[points.length - 1];
    let first = end;
    for (let i = points.length - 2; i >= 0; i--) {
      if (points[i].ts < end.ts - 21 * DAY_MS) break;
      if (points[i + 1].value - points[i].value > 40) break;
      first = points[i];
    }
    const spanDays = (end.ts - first.ts) / DAY_MS;
    if (spanDays < 2) return null;
    const ratePerDay = (first.value - end.value) / spanDays;
    if (ratePerDay < 0.05) return null;
    return Math.max(0, Math.round((percent - REFILL_THRESHOLD) / ratePerDay));
  }

  function buildTicks(periodId, start, end, isCompact, loc) {
    const ticks = [];
    if (periodId === '1m') {
      const fmt = new Intl.DateTimeFormat(loc, { day: '2-digit', month: '2-digit' });
      for (let t0 = start; t0 <= end; t0 += 7 * DAY_MS) ticks.push({ t: t0, label: fmt.format(t0) });
      return ticks;
    }
    const monthFmt = new Intl.DateTimeFormat(loc, { month: 'short' });
    const step = periodId === '12m' ? (isCompact ? 3 : 2) : (periodId === '6m' && isCompact ? 2 : 1);
    const d = new Date(start);
    d.setDate(1);
    d.setHours(0, 0, 0, 0);
    d.setMonth(d.getMonth() + 1);
    for (let k = 0; d.getTime() <= end; k++, d.setMonth(d.getMonth() + 1)) {
      if (k % step !== 0) continue;
      const month = monthFmt.format(d).replace('.', '');
      ticks.push({ t: d.getTime(), label: d.getMonth() === 0 ? `${month} ${String(d.getFullYear()).slice(2)}` : month });
    }
    return ticks;
  }

  function onChartMove(event) {
    if (points.length === 0) return;
    const rect = event.currentTarget.getBoundingClientRect();
    const px = event.clientX - rect.left + pad.l;
    const ts = chartStart + ((px - pad.l) / Math.max(chartWidth - pad.l - pad.r, 1)) * (chartEnd - chartStart);
    let lo = 0;
    let hi = points.length - 1;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (points[mid].ts < ts) lo = mid + 1; else hi = mid;
    }
    const best = lo > 0 && Math.abs(points[lo - 1].ts - ts) < Math.abs(points[lo].ts - ts) ? points[lo - 1] : points[lo];
    hover = { point: best, x: xOf(best.ts), y: yOf(best.value) };
  }

  let nowMs = Date.now();
  let tickInterval;
  $: t = createTranslator(lang);
  $: locale = lang === 'en' ? 'en-US' : 'de-DE';

  onMount(() => {
    fetchHistory();
    tickInterval = setInterval(() => { nowMs = Date.now(); }, 1000);
    return () => clearInterval(tickInterval);
  });

  $: nf1 = new Intl.NumberFormat(locale, { minimumFractionDigits: 1, maximumFractionDigits: 1 });
  $: nf2 = new Intl.NumberFormat(locale, { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  $: dateFmt = new Intl.DateTimeFormat(locale, { day: '2-digit', month: '2-digit', year: '2-digit' });
  $: dateTimeFmt = new Intl.DateTimeFormat(locale, { day: '2-digit', month: '2-digit', year: '2-digit', hour: '2-digit', minute: '2-digit' });
  $: dayMonthFmt = new Intl.DateTimeFormat(locale, { day: 'numeric', month: 'short' });
  $: ago = (sec) => (lang === 'en' ? `${sec}s ago` : `vor ${sec} s`);

  $: staleThresholdMs = Math.max((data.sampleIntervalSeconds || 5) * 1000 * 2, 30000);
  $: isStale = lastSensorUpdate > 0 && (nowMs - lastSensorUpdate) > staleThresholdMs;
  $: lastUpdateAgeSec = lastSensorUpdate > 0 ? Math.floor((nowMs - lastSensorUpdate) / 1000) : null;

  $: currentPercent = Number.isFinite(data?.salzstandPercent) ? data.salzstandPercent : 0;
  $: tankPercent = Math.min(100, Math.max(0, currentPercent));
  $: sensorState = !wsConnected
    ? 'disconnected'
    : data.sensorValid === false
      ? 'error'
      : isStale
        ? 'stale'
        : currentPercent <= REFILL_THRESHOLD
          ? 'refill'
          : 'ok';
  $: tone = sensorState === 'ok' ? 'ok' : sensorState === 'refill' || sensorState === 'stale' ? 'warn' : 'bad';
  $: sensorStateLabel = t(`dashboard.sensorState${sensorState.charAt(0).toUpperCase()}${sensorState.slice(1)}`);

  $: alertText = sensorState === 'disconnected'
    ? `${t('dashboard.disconnected')}${lastUpdateAgeSec !== null ? ` (${ago(lastUpdateAgeSec)})` : ''}`
    : sensorState === 'stale'
      ? `${t('dashboard.stale')} (${ago(lastUpdateAgeSec ?? 0)})`
      : sensorState === 'error'
        ? t('dashboard.sensorError')
        : sensorState === 'refill'
          ? t('dashboard.refill')
          : '';

  $: [pctInt, pctDec] = currentPercent.toFixed(1).split('.');
  $: decimalSeparator = lang === 'en' ? '.' : ',';
  $: reachDays = computeReach(history, currentPercent);
  $: reachLabel = reachDays === null ? '–'
    : reachDays === 0 ? t('dashboard.reachNow')
    : reachDays > 365 ? t('dashboard.reachOverYear')
    : t('dashboard.reachDays', { days: reachDays });
  $: refillDateLabel = reachDays === null ? '–'
    : reachDays === 0 ? t('dashboard.reachToday')
    : reachDays > 365 ? '–'
    : dayMonthFmt.format(nowMs + reachDays * DAY_MS);

  $: wifiQuality = data.wifiSignal >= -65 ? 'good' : data.wifiSignal >= -80 ? 'medium' : 'bad';
  $: wifiBars = !data.wifiSignal ? 0 : data.wifiSignal >= -55 ? 4 : data.wifiSignal >= -65 ? 3 : data.wifiSignal >= -75 ? 2 : 1;

  // Diagramm: Zeitachse von heute zurück, Prozentachse fest 0..100
  $: chartEnd = Math.floor(nowMs / 60000) * 60000;
  $: chartStart = (() => {
    const option = PERIOD_OPTIONS.find((entry) => entry.id === activePeriod) || PERIOD_OPTIONS[1];
    const d = new Date(chartEnd);
    d.setMonth(d.getMonth() - option.months);
    return d.getTime();
  })();
  $: points = history.filter((entry) => entry.ts >= chartStart);
  $: isCompactChart = chartWidth < 480;
  $: pad = { l: isCompactChart ? 34 : 42, r: isCompactChart ? 12 : 16, t: 12, b: 26 };
  $: xOf = (ts) => pad.l + ((ts - chartStart) / Math.max(chartEnd - chartStart, 1)) * Math.max(chartWidth - pad.l - pad.r, 1);
  $: yOf = (value) => pad.t + (1 - value / 100) * (CHART_HEIGHT - pad.t - pad.b);
  $: linePath = points.map((p, i) => `${i === 0 ? 'M' : 'L'}${xOf(p.ts).toFixed(1)} ${yOf(p.value).toFixed(1)}`).join('');
  $: areaPath = points.length > 0
    ? `${linePath}L${xOf(points[points.length - 1].ts).toFixed(1)} ${yOf(0)}L${xOf(points[0].ts).toFixed(1)} ${yOf(0)}Z`
    : '';
  $: latestPoint = points[points.length - 1] || null;
  $: oldestPoint = history[0] || null;
  $: xTicks = buildTicks(activePeriod, chartStart, chartEnd, isCompactChart, locale);
  $: tipLeft = hover ? Math.max(70, Math.min(chartWidth - 70, hover.x)) : 0;
  $: activePeriod, (hover = null);
</script>

<div class="dash tone-{tone}">
  {#if alertText}
    <div class="alert" role={sensorState === 'refill' ? 'status' : 'alert'}>
      <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3l10 18H2z"/><path d="M12 10v5M12 18h.01"/></svg>
      <span>{alertText}</span>
    </div>
  {/if}

  <section class="card hero">
    <div class="tank-wrap gauge">
      <div class="tank" aria-hidden="true" style="--p: {tankPercent}">
        <i class="sensor"></i>
        <div class="beam"><span>{nf2.format(data.rohdistanz)} m</span></div>
        <div class="salt"></div>
        <div class="threshold"></div>
      </div>
      <div class="scale" aria-hidden="true">
        <span style="bottom:100%">100 %</span>
        <span style="bottom:50%">50 %</span>
        <span class="warn" style="bottom:{REFILL_THRESHOLD}%">{REFILL_THRESHOLD} %</span>
        <span style="bottom:0">0 %</span>
      </div>
    </div>

    <div class="hero-text">
      <p class="state"><span class="dot"></span>{sensorStateLabel}</p>
      <p class="big val">{pctInt}<small>{decimalSeparator}{pctDec}</small><u>%</u></p>
      <p class="sub val">{t('dashboard.tankSummary', { cm: nf1.format(data.salzstandCm), height: data.behaelterhoehe })}</p>
      <div class="facts val">
        <div><span>{t('dashboard.reach')}</span><b>{reachLabel}</b></div>
        <div><span>{t('dashboard.refillOn')}</span><b>{refillDateLabel}</b></div>
        <div><span>{t('dashboard.distanceToSalt')}</span><b>{nf2.format(data.rohdistanz)} m</b></div>
      </div>
    </div>
  </section>

  <section class="card">
    <div class="card-head">
      <h2>{t('dashboard.historyTitle')}</h2>
      <div class="head-actions">
        <div class="seg" role="group" aria-label={t('dashboard.periodAria')}>
          {#each PERIOD_OPTIONS as option}
            <button aria-pressed={activePeriod === option.id} on:click={() => (activePeriod = option.id)}>{option.label}</button>
          {/each}
        </div>
        <button class="ghost" on:click={() => (showResetConfirm = true)} title={t('dashboard.resetHistory')}>
          <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"/></svg>
          <span class="ghost-label">{t('dashboard.resetHistory')}</span>
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

    <div class="chart" bind:clientWidth={chartWidth}>
      {#if chartWidth > 80}
        <svg width={chartWidth} height={CHART_HEIGHT} viewBox="0 0 {chartWidth} {CHART_HEIGHT}" role="img" aria-label={t('dashboard.chartAria')}>
          <defs>
            <linearGradient id="level-area" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0" style="stop-color:var(--accent);stop-opacity:0.3" />
              <stop offset="1" style="stop-color:var(--accent);stop-opacity:0" />
            </linearGradient>
          </defs>

          {#each Y_GRID as g}
            <line class="grid" x1={pad.l} x2={chartWidth - pad.r} y1={yOf(g)} y2={yOf(g)} />
            <text class="axis" x={pad.l - 8} y={yOf(g) + 4} text-anchor="end">{g}{isCompactChart ? '' : ' %'}</text>
          {/each}
          {#each xTicks as tick}
            <text class="axis" x={xOf(tick.t)} y={CHART_HEIGHT - 6} text-anchor="middle">{tick.label}</text>
          {/each}

          <line class="thr" x1={pad.l} x2={chartWidth - pad.r} y1={yOf(REFILL_THRESHOLD)} y2={yOf(REFILL_THRESHOLD)} />
          <text class="thr-text" x={chartWidth - pad.r} y={yOf(REFILL_THRESHOLD) + 13} text-anchor="end">{t('dashboard.refillBelow', { n: REFILL_THRESHOLD })}</text>

          {#if areaPath}
            <path d={areaPath} fill="url(#level-area)" />
            <path class="line" d={linePath} />
          {/if}
          {#if latestPoint}
            <circle class="halo" cx={xOf(latestPoint.ts)} cy={yOf(latestPoint.value)} r="9" />
            <circle class="dot-end" cx={xOf(latestPoint.ts)} cy={yOf(latestPoint.value)} r="5" />
          {/if}
          {#if hover}
            <line class="cross" x1={hover.x} x2={hover.x} y1={pad.t} y2={CHART_HEIGHT - pad.b} />
            <circle class="dot-end" cx={hover.x} cy={hover.y} r="4.5" />
          {/if}
          <!-- svelte-ignore a11y-no-static-element-interactions -->
          <rect
            class="hit"
            x={pad.l}
            y={pad.t}
            width={Math.max(chartWidth - pad.l - pad.r, 0)}
            height={CHART_HEIGHT - pad.t - pad.b}
            on:pointermove={onChartMove}
            on:pointerdown={onChartMove}
            on:pointerleave={() => (hover = null)}
          />
        </svg>
        {#if hover}
          <div class="tip" style="left:{tipLeft}px; top:{hover.y}px; transform:{hover.y < 48 ? 'translate(-50%, 35%)' : 'translate(-50%, -125%)'}">
            {dateTimeFmt.format(hover.point.ts)} · {nf1.format(hover.point.value)} %
          </div>
        {/if}
      {/if}
    </div>

    <div class="chart-meta">
      <span>{t('dashboard.recordStart')} <b>{oldestPoint ? dateFmt.format(oldestPoint.ts) : t('dashboard.noHistory')}</b></span>
      <span>{t('dashboard.lastHistoryPoint')} <b>{latestPoint ? dateTimeFmt.format(latestPoint.ts) : t('dashboard.noHistory')}</b></span>
    </div>
  </section>

  <section class="card system val">
    <div>
      <span class="label">{t('dashboard.wifiSignal')}</span>
      <b class="wifi-row">
        {data.wifiSignal} dBm
        <span class="bars" role="img" aria-label="{wifiQuality === 'good' ? t('dashboard.qualityGood') : wifiQuality === 'medium' ? t('dashboard.qualityMedium') : t('dashboard.qualityBad')}">
          {#each [1, 2, 3, 4] as bar}<i class:on={bar <= wifiBars} class="q-{wifiQuality}"></i>{/each}
        </span>
      </b>
    </div>
    <div><span class="label">{t('dashboard.uptime')}</span><b>{formatUptime(data.uptime)}</b></div>
    <div><span class="label">{t('dashboard.ipAddress')}</span><b>{data.ip || '–'}</b></div>
    <div><span class="label">SSID</span><b>{data.ssid || '–'}</b></div>
  </section>
</div>

<style>
  .dash {
    container: dash / inline-size;
    display: grid;
    gap: 16px;
    --st: var(--ok);
  }

  .dash.tone-warn { --st: var(--warn); }
  .dash.tone-bad { --st: var(--bad); }
  .dash.tone-bad .gauge { opacity: 0.3; }
  .dash.tone-bad .val { opacity: 0.6; }

  .card {
    background: var(--surface);
    border: 1px solid var(--line);
    border-radius: 16px;
    padding: 20px;
    min-width: 0;
    box-shadow: var(--shadow);
  }

  h2 {
    margin: 0;
    font-size: 1.05rem;
    color: var(--ink);
  }

  /* Nur für die kleinen Icons; das Diagramm-SVG setzt Füllung und Strich je Element selbst. */
  .alert svg,
  .ghost svg {
    fill: none;
    stroke: currentColor;
    stroke-width: 1.8;
    stroke-linecap: round;
    stroke-linejoin: round;
  }

  .val { font-variant-numeric: tabular-nums; }

  /* Hinweis */
  .alert {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 10px 14px;
    border-radius: var(--radius);
    border: 1px solid var(--st);
    background: color-mix(in srgb, var(--st) 13%, transparent);
    color: var(--ink);
    font-weight: 600;
    font-size: 0.9rem;
  }

  .alert svg { width: 18px; height: 18px; flex: none; color: var(--st); }

  /* Behälter */
  .hero {
    display: grid;
    grid-template-columns: auto minmax(0, 1fr);
    gap: clamp(20px, 5cqi, 56px);
    align-items: center;
    padding: 28px clamp(18px, 4cqi, 44px);
  }

  .tank-wrap { display: flex; gap: 12px; align-items: stretch; }

  .tank {
    position: relative;
    width: clamp(96px, 17cqi, 172px);
    height: clamp(210px, 36cqi, 330px);
    border: 3px solid var(--tank-line);
    border-radius: 16px 16px 26px 26px;
    overflow: hidden;
    background: var(--surface-2);
  }

  .salt {
    position: absolute;
    left: 0;
    right: 0;
    bottom: 0;
    height: calc(var(--p) * 1%);
    transition: height 0.9s cubic-bezier(0.3, 0.7, 0.2, 1);
    background:
      radial-gradient(circle at 25% 30%, var(--salt-dots) 0 1px, transparent 1.6px) 0 0 / 9px 9px,
      radial-gradient(circle at 70% 65%, var(--salt-dots) 0 1px, transparent 1.6px) 4px 5px / 11px 11px,
      var(--salt);
  }

  .salt::before {
    content: '';
    position: absolute;
    left: -12%;
    right: -12%;
    top: -9px;
    height: 18px;
    border-radius: 50%;
    background: var(--salt);
  }

  .sensor {
    position: absolute;
    left: 50%;
    top: 0;
    width: 30px;
    height: 11px;
    margin-left: -15px;
    background: var(--accent);
    border-radius: 0 0 5px 5px;
    z-index: 2;
  }

  .beam {
    position: absolute;
    left: 50%;
    top: 11px;
    height: max(0px, calc((100 - var(--p)) * 1% - 11px));
    border-left: 2px dotted var(--accent);
    z-index: 1;
    transition: height 0.9s cubic-bezier(0.3, 0.7, 0.2, 1);
  }

  .beam span {
    position: absolute;
    left: 8px;
    top: 50%;
    transform: translateY(-50%);
    padding: 0 4px;
    border-radius: 4px;
    background: var(--surface-2);
    color: var(--accent);
    font-size: 0.75rem;
    font-weight: 700;
    white-space: nowrap;
  }

  .threshold {
    position: absolute;
    left: 0;
    right: 0;
    bottom: 20%;
    border-top: 2px dashed var(--warn);
    z-index: 2;
  }

  .scale {
    position: relative;
    width: 48px;
    color: var(--muted);
    font-size: 0.75rem;
    font-weight: 600;
  }

  .scale span { position: absolute; left: 0; transform: translateY(50%); white-space: nowrap; }
  .scale .warn { color: var(--warn); }

  .state {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    margin: 0 0 6px;
    font-weight: 650;
    font-size: 0.9rem;
    color: var(--ink);
  }

  .dot { width: 9px; height: 9px; border-radius: 50%; background: var(--st); flex: none; }

  .big {
    margin: 0;
    font-size: clamp(3.4rem, 13cqi, 8rem);
    font-weight: 700;
    letter-spacing: -0.045em;
    line-height: 0.95;
    color: var(--ink);
  }

  .big small,
  .big u {
    font-size: 0.42em;
    font-weight: 600;
    letter-spacing: 0;
    color: var(--muted);
    text-decoration: none;
  }

  .big u { margin-left: 0.12em; }

  .sub { margin: 8px 0 0; color: var(--muted); }

  .facts {
    display: grid;
    grid-template-columns: repeat(3, minmax(0, 1fr));
    gap: 14px;
    margin-top: 22px;
  }

  .facts div { border-top: 1px solid var(--line); padding-top: 10px; min-width: 0; }
  .facts span { display: block; font-size: 0.76rem; color: var(--muted); font-weight: 600; }
  .facts b { display: block; font-size: 1.1rem; color: var(--ink); }

  /* Verlauf */
  .card-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    flex-wrap: wrap;
    margin-bottom: 14px;
  }

  .head-actions { display: flex; gap: 8px; flex-wrap: wrap; align-items: center; }

  .seg {
    display: inline-flex;
    gap: 2px;
    padding: 2px;
    border: 1px solid var(--line);
    border-radius: calc(var(--radius) + 2px);
    background: var(--surface-2);
  }

  .seg button {
    border: 0;
    background: none;
    padding: 5px 11px;
    border-radius: var(--radius);
    color: var(--muted);
    font: inherit;
    font-weight: 600;
    font-size: 0.82rem;
    cursor: pointer;
  }

  .seg button[aria-pressed='true'] { background: var(--ink); color: var(--bg); }

  .ghost {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    border: 1px solid var(--line);
    background: none;
    padding: 5px 10px;
    border-radius: var(--radius);
    color: var(--muted);
    font: inherit;
    font-size: 0.8rem;
    font-weight: 600;
    cursor: pointer;
  }

  .ghost:hover { color: var(--bad); border-color: var(--bad); }
  .ghost svg { width: 14px; height: 14px; }

  .reset-confirm {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
    margin-bottom: 14px;
    padding: 12px 16px;
    border-radius: var(--radius);
    border: 1px solid var(--bad);
    background: color-mix(in srgb, var(--bad) 10%, transparent);
  }

  .reset-confirm p { margin: 0; font-size: 0.9rem; color: var(--ink); font-weight: 600; }
  .reset-confirm small { font-weight: 500; color: var(--muted); }
  .reset-confirm-actions { display: flex; gap: 8px; flex-shrink: 0; }

  .btn-danger,
  .btn-cancel {
    padding: 8px 14px;
    border-radius: var(--radius);
    font: inherit;
    font-weight: 700;
    cursor: pointer;
  }

  .btn-danger { border: 0; background: var(--bad); color: #fff; }
  .btn-cancel { border: 1px solid var(--line); background: var(--surface-2); color: var(--muted); }

  .chart { position: relative; min-height: 250px; }
  .chart svg { display: block; max-width: 100%; }
  .grid { stroke: var(--line); stroke-width: 1; }
  .axis { fill: var(--muted); font-size: 11px; font-variant-numeric: tabular-nums; }
  .line { fill: none; stroke: var(--accent); stroke-width: 2.2; stroke-linejoin: round; stroke-linecap: round; }
  .thr { stroke: var(--warn); stroke-width: 1; stroke-dasharray: 4 4; }
  .thr-text { fill: var(--warn); font-size: 11px; font-weight: 600; }
  .halo { fill: var(--accent); opacity: 0.2; }
  .dot-end { fill: var(--accent); stroke: var(--surface); stroke-width: 2; }
  .cross { stroke: var(--muted); stroke-width: 1; stroke-dasharray: 3 3; }
  .hit { fill: transparent; touch-action: pan-y; }

  .tip {
    position: absolute;
    pointer-events: none;
    padding: 5px 9px;
    border-radius: 6px;
    background: var(--ink);
    color: var(--bg);
    font-size: 12px;
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }

  .chart-meta {
    display: flex;
    flex-wrap: wrap;
    gap: 4px 22px;
    margin-top: 8px;
    color: var(--muted);
    font-size: 0.82rem;
  }

  .chart-meta b { color: var(--ink); font-weight: 600; }

  /* System */
  .system {
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 14px;
  }

  .system > div { display: grid; gap: 3px; min-width: 0; align-content: start; }
  .label { font-size: 0.78rem; font-weight: 600; color: var(--muted); }
  .system b { font-size: 1.05rem; color: var(--ink); overflow-wrap: anywhere; }
  .wifi-row { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }

  .bars { display: inline-flex; align-items: flex-end; gap: 2px; height: 14px; }
  .bars i { width: 4px; background: var(--line); border-radius: 1px; }
  .bars i:nth-child(1) { height: 30%; }
  .bars i:nth-child(2) { height: 55%; }
  .bars i:nth-child(3) { height: 78%; }
  .bars i:nth-child(4) { height: 100%; }
  .bars i.on.q-good { background: var(--ok); }
  .bars i.on.q-medium { background: var(--warn); }
  .bars i.on.q-bad { background: var(--bad); }

  @container dash (max-width: 720px) {
    .card { padding: 16px; }
    .hero { padding: 18px 16px; gap: 16px; }
    .facts { grid-template-columns: 1fr 1fr; }
    .facts div:last-child { grid-column: 1 / -1; }
    .system { grid-template-columns: 1fr 1fr; }
    .scale { width: 42px; }
    .ghost-label { display: none; }
    .reset-confirm { flex-direction: column; align-items: stretch; }
  }

  @media (prefers-reduced-motion: reduce) {
    .salt,
    .beam { transition: none; }
  }
</style>
