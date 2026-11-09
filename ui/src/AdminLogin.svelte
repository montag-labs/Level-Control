<script>
  import { activeAdminLogin, adminSessionActive, logoutAdmin } from './adminAuth.js';
  import { translate as t, tr } from './i18n.js';

  let password = '';
  let confirmation = '';
  let error = '';
  let passwordInput;

  // Cursor springt beim Öffnen direkt ins Passwortfeld.
  $: if ($activeAdminLogin && passwordInput) passwordInput.focus();

  function close() {
    const request = $activeAdminLogin;
    const submittedPassword = password;
    password = '';
    confirmation = '';
    error = '';
    activeAdminLogin.set(null);
    request?.reject(new Error(tr('auth.aborted')));
  }

  function submit() {
    const request = $activeAdminLogin;
    if (!request) return;
    const submittedPassword = password;
    if (password.length < 8) {
      error = tr('auth.tooShort');
      return;
    }
    if (!request.configured && password !== confirmation) {
      error = tr('auth.mismatch');
      return;
    }
    password = '';
    confirmation = '';
    error = '';
    activeAdminLogin.set(null);
    request.resolve({ password: submittedPassword });
  }

</script>

{#if $adminSessionActive && !$activeAdminLogin}
  <div class="admin-session-tools">
    <button type="button" on:click={logoutAdmin}>{$t('auth.logout')}</button>
  </div>
{/if}

{#if $activeAdminLogin}
  <div class="admin-login-backdrop" role="presentation">
    <form class="admin-login" on:submit|preventDefault={submit}>
      <h2>{$activeAdminLogin.configured ? $t('auth.loginTitle') : $t('auth.setupTitle')}</h2>
      <p>{$activeAdminLogin.configured ? $t('auth.loginHint') : $t('auth.setupHint')}</p>
      <label>{$t('auth.password')}<input type="password" bind:this={passwordInput} bind:value={password} name="password" autocomplete={ $activeAdminLogin.configured ? 'current-password' : 'new-password' } /></label>
      {#if !$activeAdminLogin.configured}
        <label>{$t('auth.repeat')}<input type="password" bind:value={confirmation} autocomplete="new-password" /></label>
      {/if}
      {#if error}<p class="admin-login-error" role="alert">{error}</p>{/if}
      <div class="admin-login-actions">
        <button type="button" on:click={close}>{$t('auth.cancel')}</button>
        <button class="primary" type="submit">{$t('auth.continue')}</button>
      </div>
    </form>
  </div>
{/if}

<style>
  .admin-login-backdrop { position: fixed; inset: 0; z-index: 100; display: grid; place-items: center; padding: 20px; background: rgba(3, 8, 18, 0.7); backdrop-filter: blur(6px); }
  .admin-login { width: min(420px, 100%); display: grid; gap: 14px; padding: 24px; border: 1px solid var(--surface-border); border-radius: 16px; background: var(--card-grad); color: var(--text-main); box-shadow: var(--shadow); }
  .admin-login h2, .admin-login p { margin: 0; }
  .admin-login label { display: grid; gap: 6px; color: var(--text-muted); font-weight: 700; }
  .admin-login input { min-height: 40px; padding: 0 10px; border: 1px solid var(--surface-border); border-radius: 8px; background: var(--surface-2); color: var(--text-main); }
  .admin-login-error { color: #fca5a5; }
  .admin-login-actions { display: flex; justify-content: flex-end; gap: 8px; }
  .admin-login-actions button { min-height: 40px; padding: 0 18px; border: 1px solid var(--surface-border); border-radius: 10px; font-size: 0.9rem; font-weight: 600; background: var(--button-bg); color: var(--button-text); cursor: pointer; }
  .admin-login-actions .primary { border-color: transparent; background: var(--button-active-bg); color: var(--button-active-text); }
  .admin-session-tools { position: fixed; right: 16px; bottom: 16px; z-index: 45; display: flex; gap: 6px; }
  .admin-session-tools button { min-height: 32px; padding: 0 10px; border: 1px solid var(--surface-border); border-radius: 8px; background: var(--surface-2); color: var(--text-muted); cursor: pointer; }
</style>