<script>
  import { translate as t } from './i18n.js';
  export let sensorType = 'rcwl1670';
  export let note = '';

  $: isRadar = sensorType === 'rcwl1670';
</script>

<div class="sensor-sketch" aria-label={$t('sketch.aria')}>
  <svg viewBox="0 0 400 148" role="img" aria-label={isRadar ? $t('sketch.radar') : $t('sketch.laser')}>
    <!-- Basis bleibt identisch, nur Sensordarstellung wird dynamisch getauscht. -->
    <rect x="120" y="30" width="160" height="110" rx="8" fill="rgba(255,255,255,0.08)" stroke="currentColor" stroke-width="2" />
    <line x1="130" y1="118" x2="270" y2="118" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3" />

    <rect x="165" y="35" width="70" height="18" rx="3" fill="rgba(100,150,255,0.2)" stroke="currentColor" stroke-width="1.5" />

    {#if isRadar}
      <circle cx="180" cy="44" r="3" fill="var(--accent)" />
      <circle cx="220" cy="44" r="3" fill="var(--accent)" />

      <ellipse cx="180" cy="54" rx="8" ry="5" fill="none" stroke="rgba(100,150,255,0.4)" stroke-width="1" />
      <ellipse cx="180" cy="61" rx="13" ry="8" fill="none" stroke="rgba(100,150,255,0.3)" stroke-width="1" />
      <ellipse cx="180" cy="68" rx="18" ry="11" fill="none" stroke="rgba(100,150,255,0.2)" stroke-width="1" />
      <ellipse cx="220" cy="54" rx="8" ry="5" fill="none" stroke="rgba(100,150,255,0.4)" stroke-width="1" />
      <ellipse cx="220" cy="61" rx="13" ry="8" fill="none" stroke="rgba(100,150,255,0.3)" stroke-width="1" />
      <ellipse cx="220" cy="68" rx="18" ry="11" fill="none" stroke="rgba(100,150,255,0.2)" stroke-width="1" />
    {:else}
      <circle cx="200" cy="44" r="3" fill="var(--accent)" />
      <line x1="200" y1="47" x2="200" y2="114" stroke="rgba(100,150,255,0.6)" stroke-width="2.2" stroke-dasharray="4 4" />
      <polygon points="196,109 204,109 200,118" fill="rgba(100,150,255,0.75)" />
      <circle cx="200" cy="118" r="5" fill="rgba(100,150,255,0.2)" stroke="rgba(100,150,255,0.6)" stroke-width="1.4" />
    {/if}

    <line x1="112" y1="65" x2="112" y2="82" stroke="var(--accent)" stroke-width="2" />
    <polygon points="108,67 116,67 112,59" fill="var(--accent)" />
    <polygon points="108,80 116,80 112,88" fill="var(--accent)" />
    <text x="105" y="77" text-anchor="end" fill="var(--accent)" font-size="13" font-weight="600">{$t('sketch.offset')}</text>

    <line x1="112" y1="92" x2="112" y2="125" stroke="var(--accent)" stroke-width="2" />
    <polygon points="108,94 116,94 112,86" fill="var(--accent)" />
    <polygon points="108,123 116,123 112,131" fill="var(--accent)" />
    <text x="105" y="113" text-anchor="end" fill="var(--accent)" font-size="13" font-weight="600">{$t('sketch.height')}</text>

    <text x="200" y="22" text-anchor="middle" fill="var(--text-muted)" font-size="11">{$t('sketch.sensor')}</text>
    <text x="200" y="137" text-anchor="middle" fill="var(--text-muted)" font-size="11">{$t('sketch.floor')}</text>
  </svg>
  <p class="sketch-note">{note}</p>
  <ol class="calibration-steps" aria-label="Kalibrierungsablauf">
    <li>{$t('sketch.step1')}</li>
    <li>{$t('sketch.step2')}</li>
    <li>{$t('sketch.step3')}</li>
    <li>{$t('sketch.step4')}</li>
  </ol>
</div>

<style>
  .sensor-sketch {
    margin-top: 14px;
    margin-bottom: 8px;
    padding: 10px;
    border: 1px solid var(--surface-border);
    border-radius: 12px;
    background: rgba(255, 255, 255, 0.05);
    color: var(--text-main);
  }

  .sensor-sketch svg {
    width: 100%;
    max-width: 500px;
    height: auto;
    display: block;
    margin: 0 auto;
  }

  .sketch-note {
    margin: 2px 0 2px;
    font-size: 0.85rem;
    color: var(--text-muted);
    text-align: center;
  }

  .calibration-steps {
    display: grid;
    gap: 4px;
    max-width: 500px;
    margin: 10px auto 2px;
    padding-left: 24px;
    color: var(--text-muted);
    font-size: 0.82rem;
    line-height: 1.45;
  }
</style>
