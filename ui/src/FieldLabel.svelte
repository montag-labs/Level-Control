<script>
  import { translate as t } from './i18n.js';
  export let text = '';
  export let required = false;
</script>

<span class="field-label">
  {text}
  {#if required}
    <span class="required-mark" aria-label={$t('ui.required')} title={$t('ui.required')}>*</span>
  {/if}
</span>

<style>
  .field-label {
    display: inline-flex;
    align-items: center;
    gap: 0.2rem;
    white-space: nowrap;
  }

  .required-mark {
    color: #ff5c5c;
    font-weight: 700;
    line-height: 1;
  }
</style>