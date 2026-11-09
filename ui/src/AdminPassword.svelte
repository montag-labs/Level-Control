<script>
  import { changeAdminPassword } from './adminAuth.js';
  import { showNotice } from './dialogStore.js';
  import { createTranslator } from './i18n.js';

  export let lang = 'de';
  let password = '';
  let confirmation = '';
  let saving = false;
  $: t = createTranslator(lang);

  async function savePassword() {
    if (password.length < 8) {
      showNotice('error', t('admin.passwordTooShort'));
      return;
    }
    if (password !== confirmation) {
      showNotice('error', t('admin.passwordMismatch'));
      return;
    }

    saving = true;
    try {
      await changeAdminPassword(password);
      password = '';
      confirmation = '';
      showNotice('success', t('admin.passwordChanged'));
    } catch (error) {
      showNotice('error', error?.message || t('admin.passwordChangeFailed'));
    } finally {
      saving = false;
    }
  }
</script>

<section class="password-module" aria-labelledby="password-title">
  <div class="module-heading">
    <span class="eyebrow">{t('admin.passwordEyebrow')}</span>
    <h3 id="password-title">{t('admin.passwordTitle')}</h3>
    <p>{t('admin.passwordSubtitle')}</p>
  </div>

  <form on:submit|preventDefault={savePassword}>
    <label>
      {t('admin.newPassword')}
      <input type="password" bind:value={password} autocomplete="new-password" minlength="8" disabled={saving} />
    </label>
    <label>
      {t('admin.repeatPassword')}
      <input type="password" bind:value={confirmation} autocomplete="new-password" minlength="8" disabled={saving} />
    </label>
    <button class="primary" type="submit" disabled={saving}>
      {saving ? t('admin.passwordSaving') : t('admin.passwordSave')}
    </button>
  </form>
</section>

<style>
  .password-module { max-width: 560px; display: grid; gap: 22px; padding: 22px; border: 1px solid var(--surface-border); border-radius: 12px; background: var(--card-grad); box-shadow: var(--shadow); }
  .module-heading h3 { margin: 4px 0 8px; color: var(--text-main); font-size: 1.35rem; }
  .module-heading p { margin: 0; color: var(--text-muted); line-height: 1.5; }
  .eyebrow { color: var(--accent); font-size: 0.75rem; font-weight: 800; letter-spacing: 0.12em; text-transform: uppercase; }
  form { display: grid; gap: 14px; }
  label { display: grid; gap: 6px; color: var(--text-muted); font-weight: 700; }
  input { min-height: 42px; padding: 0 12px; border: 1px solid var(--surface-border); border-radius: 8px; background: var(--surface-2); color: var(--text-main); }
  button { min-height: 40px; margin-top: 6px; padding: 0 18px; border: 1px solid transparent; border-radius: 10px; background: var(--button-active-bg); color: var(--button-active-text); font-size: 0.9rem; font-weight: 600; cursor: pointer; }
  button:disabled { cursor: wait; opacity: 0.65; }
</style>
