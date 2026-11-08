<script>
  import { translate as t } from './i18n.js';
  export let value = '';
  export let hasStoredPassword = false;
  export let mask = '*****';
  export let disabled = false;

  let reveal = false;

  $: placeholder = hasStoredPassword && !value ? mask : '';
  function toggleReveal() {
    reveal = !reveal;
  }
</script>

<div class="password-input-wrap">
  {#if reveal}
    <input type="text" bind:value placeholder={placeholder} {disabled} />
  {:else}
    <input type="password" bind:value placeholder={placeholder} {disabled} />
  {/if}
  <button
    type="button"
    class="password-toggle"
    on:click={toggleReveal}
    aria-label={reveal ? $t('ui.passwordHide') : $t('ui.passwordShow')}
    title={reveal ? $t('ui.passwordHide') : $t('ui.passwordShow')}
    {disabled}
  >
    {#if reveal}
      <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 6c4.5 0 8.27 2.94 9.54 7-.54 1.75-1.56 3.29-2.9 4.45l1.4 1.4-1.41 1.42-2.01-2.01A10.7 10.7 0 0 1 12 20c-4.5 0-8.27-2.94-9.54-7a10.97 10.97 0 0 1 4.15-5.57L4.1 4.92 5.5 3.5l15 15-1.41 1.42-2.36-2.36A10.5 10.5 0 0 1 12 18c-3.15 0-5.83-1.84-7.13-5a8.68 8.68 0 0 1 3.11-3.9l1.46 1.46A4 4 0 0 0 12 16a3.96 3.96 0 0 0 1.99-.53l1.55 1.55A5.98 5.98 0 0 1 12 18c3.15 0 5.83-1.84 7.13-5a8.71 8.71 0 0 0-2.37-3.33l1.43-1.43A10.98 10.98 0 0 1 21.54 13C20.27 17.06 16.5 20 12 20z"/></svg>
    {:else}
      <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 5c4.5 0 8.27 2.94 9.54 7-1.27 4.06-5.04 7-9.54 7s-8.27-2.94-9.54-7C3.73 7.94 7.5 5 12 5zm0 2c-3.15 0-5.83 1.84-7.13 5 1.3 3.16 3.98 5 7.13 5s5.83-1.84 7.13-5c-1.3-3.16-3.98-5-7.13-5zm0 2.5A2.5 2.5 0 1 1 12 14.5 2.5 2.5 0 0 1 12 9.5z"/></svg>
    {/if}
  </button>
</div>

<style>
  .password-input-wrap {
    display: grid;
    grid-template-columns: minmax(0, 360px) auto;
    justify-content: start;
    gap: 30px;
    align-items: center;
  }

  .password-input-wrap input {
    margin-left: 0;
    width: min(360px, 100%);
    border: 1px solid var(--surface-border);
    border-radius: 10px;
    padding: 8px 10px;
    background: rgba(255, 255, 255, 0.14);
    color: var(--text-main);
  }

  .password-toggle {
    width: 40px;
    height: 40px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border: 1px solid var(--surface-border);
    border-radius: 10px;
    background: rgba(255, 255, 255, 0.08);
    color: var(--accent);
    cursor: pointer;
  }

  .password-toggle svg {
    width: 18px;
    height: 18px;
    fill: currentColor;
  }

  .password-toggle:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }

  @media (max-width: 640px) {
    .password-input-wrap {
      grid-template-columns: minmax(0, 1fr) auto;
      gap: 12px;
    }
  }
</style>