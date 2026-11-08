<script>
  import { tick } from 'svelte';
  import { translate as t } from './i18n.js';
  import { activeConfirmation, activeNotice, closeNotice, resolveConfirmation } from './dialogStore.js';

  let dialogCard;
  let lastDialog = null;

  $: if ($activeConfirmation && $activeConfirmation !== lastDialog) {
    lastDialog = $activeConfirmation;
    tick().then(() => dialogCard?.querySelector('button')?.focus());
  }

  function handleDialogKeydown(event) {
    if (!$activeConfirmation || !dialogCard) return;
    if (event.key === 'Escape') {
      event.preventDefault();
      resolveConfirmation(false);
      return;
    }
    if (event.key !== 'Tab') return;
    const focusable = [...dialogCard.querySelectorAll('button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])')]
      .filter((element) => !element.disabled);
    if (focusable.length === 0) return;
    const first = focusable[0];
    const last = focusable[focusable.length - 1];
    if (event.shiftKey && document.activeElement === first) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && document.activeElement === last) {
      event.preventDefault();
      first.focus();
    }
  }
</script>

<svelte:window on:keydown={handleDialogKeydown} />

{#if $activeNotice}
  <div class="notice-overlay" role="alert" aria-live="assertive">
    <div class="notice-card" class:error={$activeNotice.type === 'error'} class:success={$activeNotice.type === 'success'}>
      <div class="notice-icon" aria-hidden="true">
        {#if $activeNotice.type === 'error'}
          <svg viewBox="0 0 24 24"><path d="M12 2a10 10 0 1 0 10 10A10 10 0 0 0 12 2zm0 13a1.25 1.25 0 1 1-1.25 1.25A1.25 1.25 0 0 1 12 15zm1-8v6h-2V7z"/></svg>
        {:else}
          <svg viewBox="0 0 24 24"><path d="M12 2a10 10 0 1 0 10 10A10 10 0 0 0 12 2zm-1.2 13.2-3.3-3.3 1.4-1.4 1.9 1.88 4.3-4.28 1.4 1.4z"/></svg>
        {/if}
      </div>
      <div class="notice-copy">
        <strong>{$activeNotice.title}</strong>
        <p>{$activeNotice.message}</p>
      </div>
      <button class="notice-close" type="button" on:click={closeNotice} aria-label={$t('dialog.close')}>X</button>
    </div>
  </div>
{/if}

{#if $activeConfirmation}
  <div class="dialog-backdrop" role="presentation">
    <div class="dialog-card" bind:this={dialogCard} role="dialog" aria-modal="true" aria-labelledby="dialog-title" aria-describedby="dialog-message">
      <div class="dialog-icon" class:danger={$activeConfirmation.tone === 'danger'} aria-hidden="true">
        {#if $activeConfirmation.tone === 'danger'}
          <svg viewBox="0 0 24 24"><path d="M12 3 2 21h20zm0 4.4L18.6 19H5.4zM11 10h2v5h-2zm0 6.5h2v2h-2z"/></svg>
        {:else}
          <svg viewBox="0 0 24 24"><path d="M12 2a10 10 0 1 0 10 10A10 10 0 0 0 12 2zm1 15h-2v-2h2zm0-4h-2V7h2z"/></svg>
        {/if}
      </div>
      <div class="dialog-copy">
        <strong id="dialog-title">{$activeConfirmation.title}</strong>
        <p id="dialog-message">{$activeConfirmation.message}</p>
      </div>
      <div class="dialog-actions">
        <button class="dialog-btn secondary" type="button" on:click={() => resolveConfirmation(false)}>{$activeConfirmation.cancelLabel}</button>
        <button class="dialog-btn primary" class:danger={$activeConfirmation.tone === 'danger'} type="button" on:click={() => resolveConfirmation(true)}>{$activeConfirmation.confirmLabel}</button>
      </div>
    </div>
  </div>
{/if}

<style>
  .notice-overlay {
    position: fixed;
    top: 20px;
    right: 20px;
    z-index: 60;
    width: min(420px, calc(100vw - 32px));
  }

  .notice-card {
    display: grid;
    grid-template-columns: auto 1fr auto;
    gap: 14px;
    align-items: start;
    padding: 16px;
    border-radius: 16px;
    border: 1px solid rgba(255, 255, 255, 0.14);
    box-shadow: 0 16px 40px rgba(0, 0, 0, 0.28);
    backdrop-filter: blur(10px);
  }

  .notice-card.error {
    background: rgba(132, 28, 28, 0.92);
    border-color: rgba(248, 113, 113, 0.45);
  }

  .notice-card.success {
    background: rgba(20, 83, 45, 0.92);
    border-color: rgba(74, 222, 128, 0.4);
  }

  .notice-copy strong,
  .dialog-copy strong {
    display: block;
    margin-bottom: 6px;
    color: #fff;
  }

  .notice-copy p,
  .dialog-copy p {
    margin: 0;
    color: rgba(255, 255, 255, 0.92);
    line-height: 1.5;
  }

  .notice-close,
  .dialog-btn {
    min-height: 40px;
    border-radius: 999px;
    border: 1px solid rgba(255, 255, 255, 0.2);
    background: rgba(255, 255, 255, 0.1);
    color: #fff;
    font-weight: 800;
    cursor: pointer;
    transition: transform 0.18s ease, background 0.18s ease;
  }

  .notice-close {
    min-width: 40px;
    padding: 0;
    font-size: 0.92rem;
  }

  .notice-close:hover,
  .dialog-btn:hover {
    transform: translateY(-1px);
    background: rgba(255, 255, 255, 0.18);
  }

  .notice-icon,
  .dialog-icon {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 42px;
    height: 42px;
    border-radius: 14px;
    background: rgba(255, 255, 255, 0.12);
    border: 1px solid rgba(255, 255, 255, 0.14);
    color: #fff;
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.04);
  }

  .notice-icon svg,
  .dialog-icon svg {
    width: 22px;
    height: 22px;
    fill: currentColor;
  }

  .dialog-backdrop {
    position: fixed;
    inset: 0;
    z-index: 55;
    display: grid;
    place-items: center;
    padding: 20px;
    background: rgba(3, 8, 18, 0.58);
    backdrop-filter: blur(6px);
  }

  .dialog-card {
    width: min(520px, 100%);
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 16px;
    padding: 22px;
    border-radius: 20px;
    border: 1px solid rgba(255, 255, 255, 0.14);
    background: linear-gradient(160deg, rgba(18, 37, 73, 0.97) 0%, rgba(10, 23, 48, 0.97) 100%);
    box-shadow: 0 24px 60px rgba(0, 0, 0, 0.35);
  }

  .dialog-icon.danger {
    background: rgba(248, 113, 113, 0.14);
    border-color: rgba(248, 113, 113, 0.28);
  }

  .dialog-copy {
    min-width: 0;
  }

  .dialog-actions {
    grid-column: 2;
    display: flex;
    justify-content: flex-end;
    gap: 10px;
    margin-top: 18px;
  }

  .dialog-btn {
    padding: 0 16px;
  }

  .dialog-btn.primary {
    background: rgba(98, 184, 221, 0.2);
    border-color: rgba(98, 184, 221, 0.45);
  }

  .dialog-btn.primary.danger {
    background: rgba(248, 113, 113, 0.18);
    border-color: rgba(248, 113, 113, 0.42);
  }

  @media (max-width: 840px) {
    .notice-overlay {
      top: 12px;
      right: 16px;
      left: 16px;
      width: auto;
    }

    .dialog-card {
      grid-template-columns: 1fr;
      padding: 18px;
    }

    .dialog-icon {
      width: 46px;
      height: 46px;
    }

    .dialog-actions {
      grid-column: auto;
      flex-direction: column-reverse;
    }

    .dialog-btn {
      width: 100%;
    }
  }
</style>