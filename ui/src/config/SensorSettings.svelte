<script>
  import { onMount } from 'svelte';
  import { adminFetch } from '../adminAuth.js';
  import { showNotice } from '../dialogStore.js';
  import FieldLabel from '../FieldLabel.svelte';
  import { createTranslator } from '../i18n.js';
  import SensorSchema from '../SensorSchema.svelte';
  import ConfigPanel from './ConfigPanel.svelte';
  import { LIMITS } from './helpers.js';

  let { data = $bindable(), loadConfig, lang = 'de', onDirtyStateChange = () => {} } = $props();
  const t = $derived(createTranslator(lang));

  const UNITS = [
    { value: 'seconds', key: 'unitSeconds', seconds: 1 },
    { value: 'minutes', key: 'unitMinutes', seconds: 60 },
    { value: 'hours', key: 'unitHours', seconds: 3600 },
    { value: 'days', key: 'unitDays', seconds: 86400 }
  ];
  const SENSOR_TYPES = [
    { value: 'rcwl1670', key: 'typeUltrasonic' },
    { value: 'vl53l1x', key: 'typeToF' }
  ];

  let sensorType = $state('rcwl1670');
  let offset = $state('0');
  let height = $state('95');
  let intervalValue = $state('5');
  let intervalUnit = $state('seconds');
  let dirty = $state(false);
  let saving = $state(false);
  let intervalChecked = $state(false);
  let laserText = $state('');

  const intervalSeconds = $derived.by(() => {
    const value = Number(intervalValue);
    const unit = UNITS.find((entry) => entry.value === intervalUnit)?.seconds || 1;
    return Number.isFinite(value) && value > 0 ? Math.round(value * unit) : 0;
  });
  const intervalError = $derived(intervalChecked && intervalSeconds < LIMITS.minSampleSeconds);

  function setIntervalFromSeconds(totalSeconds) {
    const seconds = Math.max(LIMITS.minSampleSeconds, Number(totalSeconds) || LIMITS.minSampleSeconds);
    const unit = [...UNITS].reverse().find((entry) => seconds % entry.seconds === 0) || UNITS[0];
    intervalUnit = unit.value;
    intervalValue = String(seconds / unit.seconds);
  }

  // Gerätewerte übernehmen, solange der Benutzer nichts bearbeitet hat.
  $effect(() => {
    if (dirty) return;
    sensorType = data.sensorType || 'rcwl1670';
    offset = String(data.offset ?? '');
    height = String(data.behaelterhoehe ?? '');
    if (data.sampleIntervalSeconds) {
      setIntervalFromSeconds(data.sampleIntervalSeconds);
    }
  });

  $effect(() => {
    onDirtyStateChange(dirty);
  });

  // Laser-Version (nur VL53L1X): Register-ID oder Klartext aus dem Backend einordnen.
  $effect(() => {
    if (sensorType !== 'vl53l1x') {
      laserText = '';
      return;
    }
    let cancelled = false;
    fetch('/api/laser-version')
      .then((response) => response.json())
      .then((payload) => {
        if (cancelled) return;
        if (!payload?.version) {
          laserText = t('config.sensor.laserNotDetected');
          return;
        }
        const version = String(payload.version).toUpperCase();
        if (version === '0XC0' || version === '0XEE' || version === 'VL53L0X') {
          laserText = t('config.sensor.laserValid', { name: 'VL53L0X' });
        } else if (version === '0X010F' || version === '0XEA' || version === 'VL53L1X') {
          laserText = t('config.sensor.laserValid', { name: 'VL53L1X' });
        } else {
          laserText = t('config.sensor.laserUnknown', { value: payload.version });
        }
      })
      .catch(() => {
        if (!cancelled) laserText = t('config.sensor.laserReadError');
      });
    return () => { cancelled = true; };
  });

  async function save() {
    if (saving) return false;
    intervalChecked = true;
    if (intervalSeconds < LIMITS.minSampleSeconds) {
      showNotice('error', t('config.sensor.intervalSaveError'));
      return false;
    }
    const tankHeight = Number(height);
    if (!(tankHeight >= LIMITS.minTankHeightCm && tankHeight <= LIMITS.maxTankHeightCm)) {
      showNotice('error', t('config.sensor.heightRange', { min: LIMITS.minTankHeightCm, max: LIMITS.maxTankHeightCm }));
      return false;
    }
    if (!(Math.abs(Number(offset)) <= LIMITS.maxOffsetCm)) {
      showNotice('error', t('config.sensor.offsetRange', { max: LIMITS.maxOffsetCm }));
      return false;
    }

    saving = true;
    try {
      const response = await adminFetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ behaelterhoehe: tankHeight, offset: Number(offset), sampleIntervalSeconds: intervalSeconds, sensorType })
      });
      if (!response.ok) {
        showNotice('error', t('config.sensor.saveFailed'));
        return false;
      }
      showNotice('success', t('config.sensor.saved'));
      data = { ...data, behaelterhoehe: tankHeight, offset: Number(offset), sampleIntervalSeconds: intervalSeconds, sensorType };
      intervalChecked = false;
      dirty = false;
      return true;
    } catch (_) {
      showNotice('error', t('config.sensor.saveFailed'));
      return false;
    } finally {
      saving = false;
    }
  }

  function discard() {
    intervalChecked = false;
    dirty = false;
    loadConfig();
  }

  onMount(() => { loadConfig(); });
</script>

<ConfigPanel {t} title={t('config.sensor.title')} {dirty} {saving} onsave={save} ondiscard={discard} onedit={() => { dirty = true; }}>
  <label class="field-row">
    <span>{t('config.sensor.hardware')}</span>
    <select bind:value={sensorType}>
      {#each SENSOR_TYPES as option}
        <option value={option.value}>{t(`config.sensor.${option.key}`)}</option>
      {/each}
    </select>
  </label>

  {#if sensorType === 'vl53l1x'}
    <div class="field-row">
      <span>{t('config.sensor.laserVersion')}</span>
      <span>{laserText}</span>
    </div>
  {/if}

  <label class="field-row">
    <span>{t('config.sensor.offset')}</span>
    <input class="sensor-fixed-width" type="number" min={-LIMITS.maxOffsetCm} max={LIMITS.maxOffsetCm} bind:value={offset} />
  </label>

  <label class="field-row">
    <span>{t('config.sensor.height')}</span>
    <input class="sensor-fixed-width" type="number" min={LIMITS.minTankHeightCm} max={LIMITS.maxTankHeightCm} bind:value={height} />
  </label>

  <label class="field-row field-row--top-align">
    <span><FieldLabel text={t('config.sensor.sampleRate')} required={true} /></span>
    <div class="field-control field-control--inline sensor-inline-width">
      <input
        type="number"
        min="1"
        step="1"
        bind:value={intervalValue}
        aria-required="true"
        aria-invalid={intervalError}
        class:input-invalid={intervalError}
        onblur={() => { intervalChecked = true; }}
      />
      <select bind:value={intervalUnit} onblur={() => { intervalChecked = true; }}>
        {#each UNITS as unit}
          <option value={unit.value}>{t(`config.sensor.${unit.key}`)}</option>
        {/each}
      </select>
      {#if intervalError}
        <p class="field-error field-error--inline">{t('config.sensor.intervalTooSmall')}</p>
      {:else}
        <p class="helper-text field-help">{t('config.sensor.minRateHint')}</p>
      {/if}
    </div>
  </label>

  <SensorSchema sensorType={sensorType} note={t('config.sensor.sketchNote')} />

  <button class="primary" onclick={save}>{t('config.common.save')}</button>
</ConfigPanel>
