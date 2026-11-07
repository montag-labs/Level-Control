<script>
  // Gemeinsamer Rahmen der Konfigurationsmodule: Speicherleiste (Speichern/Verwerfen), Abschnittsrahmen und die
  // gemeinsamen Formularstile. Die Module liefern nur Inhalt und Logik.
  let { t, title, dirty = false, saving = false, onsave, ondiscard, onedit, children } = $props();
</script>

<div class="save-bar" class:save-bar--dirty={dirty}>
  <span>{dirty ? t('config.common.unsaved') : t('config.common.allSaved')}</span>
  <div class="save-bar-actions">
    <button type="button" class="save-bar-discard" disabled={!dirty || saving} onclick={ondiscard}>{t('config.common.discard')}</button>
    <button type="button" class="save-bar-save" disabled={!dirty || saving} onclick={onsave}>
      {saving ? t('config.common.saving') : t('config.common.save')}
    </button>
  </div>
</div>

<div class="config-section" oninput={onedit} onchange={onedit}>
  <h2>{title}</h2>
  {@render children()}
</div>

<style>
  .save-bar {

    position: sticky;

    top: 0;

    z-index: 5;

    display: flex;

    align-items: center;

    justify-content: space-between;

    gap: 12px;

    margin: -4px 0 14px;

    padding: 10px 12px;

    border: 1px solid var(--surface-border);

    border-radius: 10px;

    background: var(--surface-2);

    color: var(--text-muted);

    font-size: 0.86rem;

    font-weight: 700;

  }
  .save-bar--dirty {

    border-color: rgba(245, 158, 11, 0.45);

    color: var(--text-main);

  }
  .save-bar-actions {

    display: flex;

    gap: 8px;

  }
  .save-bar button {

    min-height: 34px;

    padding: 0 11px;

    border: 1px solid var(--surface-border);

    border-radius: 7px;

    font-weight: 700;

    cursor: pointer;

  }
  .save-bar button:disabled {

    cursor: default;

    opacity: 0.45;

  }
  .save-bar-discard {

    background: transparent;

    color: var(--text-muted);

  }
  .save-bar-save {

    background: var(--button-active-bg);

    color: var(--button-active-text);

  }

  .config-section {

    border: 1px solid var(--surface-border);

    padding: 16px;

    border-radius: 14px;

    background: var(--card-grad);

    box-shadow: var(--shadow);

  }
  .config-section :global(h2) {

    margin: 0 0 8px 0;

    color: var(--text-main);

  }
  .config-section :global(h3) {

    margin-top: 20px;

    margin-bottom: 8px;

    color: var(--accent);

  }
  .config-section :global(p) {

    color: var(--text-muted);

    margin-top: 0;

  }
  .config-section :global(label) {

    display: block;

    margin: 10px 0 6px;

    color: var(--text-main);

    font-weight: 600;

  }
  .config-section :global(.field-row) {

    display: grid;

    grid-template-columns: minmax(150px, 200px) 1fr;

    align-items: center;

    gap: 12px;

    margin: 10px 0;

  }
  .config-section :global(.field-row--top-align) {

    align-items: start;

  }
  .config-section :global(.field-row span) {

    white-space: nowrap;

    text-align: left;

    padding-left: 0;

  }
  .config-section :global(.field-control) {

    width: min(360px, 100%);

  }
  .config-section :global(.field-control--choice) {

    width: min(560px, 100%);

  }
  .config-section :global(.field-control--inline) {

    display: grid;

    grid-template-columns: 80px minmax(0, 1fr);

    gap: 30px;

    align-items: start;

    width: min(360px, 100%);

  }
  .config-section :global(.sensor-fixed-width) {

    width: min(250px, 100%);

  }
  .config-section :global(.sensor-inline-width) {

    width: min(250px, 100%);

    grid-template-columns: 70px 150px;

    gap: 30px;

  }
  .config-section :global(.field-control--inline.sensor-inline-width input) {

    width: 70px;

  }
  .config-section :global(.field-control--inline.sensor-inline-width select) {

    width: 150px;

  }
  .config-section :global(.field-control--inline input) {

    width: 100%;

  }
  .config-section :global(input) {

    margin-left: 0;

    width: min(360px, 100%);

    border: 1px solid var(--surface-border);

    border-radius: 10px;

    padding: 8px 10px;

    background: rgba(255, 255, 255, 0.14);

    color: var(--text-main);

  }
  .config-section :global(textarea) {

    margin-left: 0;

    width: min(360px, 100%);

    border: 1px solid var(--surface-border);

    border-radius: 10px;

    padding: 8px 10px;

    background: rgba(255, 255, 255, 0.14);

    color: var(--text-main);

    resize: vertical;

    font-family: inherit;

  }
  .config-section :global(.field-control input) {

    width: 100%;

  }
  .config-section :global(.field-control--inline select) {

    width: 100px;

  }
  .config-section :global(.input-invalid) {

    border-color: #ff6b6b;

    box-shadow: 0 0 0 1px rgba(255, 107, 107, 0.25);

    background: rgba(255, 107, 107, 0.08);

  }
  .config-section :global(.field-error) {

    margin: 6px 0 0;

    font-size: 0.82rem;

    color: #ff9a8f;

  }
  .config-section :global(.field-error--inline),
  .config-section :global(.field-help) {

    grid-column: 1 / -1;

  }
  .config-section :global(select) {

    width: min(360px, 100%);

    border: 1px solid var(--surface-border);

    border-radius: 10px;

    padding: 8px 42px 8px 12px;

    background: rgba(255, 255, 255, 0.14);

    color: var(--text-main);

    appearance: none;

    background-image:

      linear-gradient(45deg, transparent 50%, var(--accent) 50%),

      linear-gradient(135deg, var(--accent) 50%, transparent 50%),

      linear-gradient(to right, transparent, transparent);

    background-position:

      calc(100% - 20px) calc(50% - 3px),

      calc(100% - 14px) calc(50% - 3px),

      100% 0;

    background-size: 6px 6px, 6px 6px, 2.5em 100%;

    background-repeat: no-repeat;

  }
  .config-section :global(.theme-select) {

    background-color: var(--surface-2);

    color: var(--text-main);

    border: 1px solid var(--surface-border);

  }
  .config-section :global(.theme-select:focus) {

    outline: none;

    border-color: var(--accent);

    box-shadow: 0 0 0 2px rgba(98, 184, 221, 0.2);

  }
  :global(html[data-theme='night']) .config-section :global(.theme-select) {

    background-color: rgba(10, 24, 48, 0.96);

    color: #e8f0ff;

  }
  :global(html[data-theme='day']) .config-section :global(.theme-select) {

    background-color: rgba(242, 255, 247, 0.96);

    color: #163326;

  }
  .config-section :global(.theme-select option) {

    color: inherit;

  }
  .config-section :global(select option) {

    background-color: #0d1b33;

    color: #e8f0ff;

  }
  :global(html[data-theme='night']) .config-section :global(select) {

    background-color: rgba(10, 24, 48, 0.92);

    color: #e8f0ff;

  }
  :global(html[data-theme='day']) .config-section :global(select option) {

    background-color: #f2fff7;

    color: #183326;

  }
  :global(html[data-theme='day']) .config-section :global(select) {

    background-color: rgba(242, 255, 247, 0.92);

    color: #163326;

  }
  .config-section :global(.input-action-row) {

    display: grid;

    grid-template-columns: minmax(0, 360px) auto;

    justify-content: start;

    gap: 30px;

    align-items: center;

  }
  .config-section :global(.icon-button) {

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
  .config-section :global(.icon-button svg) {

    width: 18px;

    height: 18px;

    fill: currentColor;

  }
  .config-section :global(.compact-row) {

    margin-top: 4px;

  }
  .config-section :global(.choice-grid) {

    display: grid;

    grid-template-columns: repeat(2, minmax(0, 1fr));

    gap: 10px;

    margin: 12px 0 4px;

  }
  .config-section :global(.choice-grid--triple) {

    grid-template-columns: repeat(3, minmax(0, 1fr));

    margin-top: 0;

  }
  .config-section :global(.choice-card) {

    display: flex;

    align-items: center;

    gap: 8px;

    padding: 10px 12px;

    border: 1px solid var(--surface-border);

    border-radius: 12px;

    background: rgba(255, 255, 255, 0.05);

    cursor: pointer;

  }
  .config-section :global(.choice-card input) {

    width: 16px;

    height: 16px;

    margin: 0;

  }
  .config-section :global(.choice-card span) {

    color: var(--text-main);

    font-weight: 600;

  }
  .config-section :global(.helper-text) {

    margin-top: 4px;

    margin-bottom: 6px;

    font-size: 0.84rem;

    color: var(--text-muted);

  }
  .config-section :global(.helper-text.error) {

    color: #ff9a8f;

  }
  .config-section :global(.checkbox-row) {

    display: flex;

    align-items: center;

    gap: 8px;

  }
  .config-section :global(.checkbox-row input) {

    margin: 0;

    width: 16px;

    height: 16px;

  }
  .config-section :global(ul) {

    margin-top: 8px;

    margin-bottom: 0;

    padding-left: 18px;

  }
  .config-section :global(.device-id-row) {

    font-size: 0.9rem;

    color: var(--text-muted);

    margin-bottom: 8px;

  }
  .config-section :global(.device-id-code) {

    font-family: monospace;

    background: rgba(98, 184, 221, 0.15);

    border-radius: 5px;

    padding: 2px 6px;

    color: var(--accent);

    font-size: 0.85rem;

    user-select: all;

    cursor: text;

  }
  .config-section :global(.topic-heading) {

    font-weight: 600;

    color: var(--text-main);

    margin-top: 14px;

    margin-bottom: 4px;

    font-size: 0.9rem;

  }
  .config-section :global(.topic-card) {

    margin-top: 14px;

    border: 1px solid var(--surface-border);

    border-radius: 12px;

    background: rgba(255, 255, 255, 0.05);

    overflow: hidden;

  }
  .config-section :global(.topic-card summary) {

    display: flex;

    align-items: center;

    justify-content: space-between;

    gap: 12px;

    padding: 12px 14px;

    cursor: pointer;

    list-style: none;

    color: var(--text-main);

    user-select: none;

  }
  .config-section :global(.topic-card summary::-webkit-details-marker) {

    display: none;

  }
  .config-section :global(.topic-card summary::after) {

    content: '▾';

    font-size: 0.9rem;

    color: var(--accent);

    transition: transform 0.2s ease;

  }
  .config-section :global(.topic-card[open] summary::after) {

    transform: rotate(180deg);

  }
  .config-section :global(.topic-card-title) {

    font-weight: 600;

    color: var(--text-main);

  }
  .config-section :global(.topic-card-meta) {

    margin-left: auto;

    font-size: 0.8rem;

    color: var(--text-muted);

  }
  .config-section :global(.topic-card-content) {

    padding: 0 14px 14px;

    border-top: 1px solid rgba(255, 255, 255, 0.06);

  }
  .config-section :global(.topic-list) {

    margin-top: 4px;

    margin-bottom: 8px;

    padding-left: 0;

    list-style: none;

  }
  .config-section :global(.topic-list li) {

    padding: 2px 0;

    font-size: 0.82rem;

    color: var(--text-muted);

    word-break: break-all;

  }
  .config-section :global(.topic-list li:not(.topic-group)) {

    padding-left: 12px;

  }
  .config-section :global(.topic-group) {

    color: var(--text-main);

    font-weight: 600;

    font-size: 0.78rem;

    text-transform: uppercase;

    letter-spacing: 0.05em;

    margin-top: 10px;

    opacity: 0.65;

  }
  .config-section :global(.topic-list code) {

    font-family: monospace;

    font-size: 0.82rem;

    background: rgba(255, 255, 255, 0.07);

    border-radius: 4px;

    padding: 1px 5px;

    color: var(--text-main);

  }
  .config-section :global(.primary) {

    margin-top: 10px;

    padding: 10px 14px;

    border: none;

    border-radius: 10px;

    background: var(--button-active-bg);

    color: var(--button-active-text);

    font-weight: 600;

    cursor: pointer;

  }
  .config-section :global(.button-row) {

    display: flex;

    gap: 10px;

    align-items: center;

    flex-wrap: wrap;

    margin-top: 10px;

  }
  .config-section :global(.button-row--push) {

    width: min(560px, 100%);

    display: grid;

    grid-template-columns: repeat(2, minmax(0, 1fr));

    gap: 12px;

  }
  .config-section :global(.checkbox-row--warning) {

    color: var(--text-muted);

    font-weight: normal;

  }
  .config-section :global(.diag-section) {

    margin: 14px 0 4px;

  }
  .config-section :global(.diag-result) {

    margin-top: 10px;

    padding: 10px 14px;

    border-radius: 10px;

    border: 1px solid var(--surface-border);

    background: rgba(255, 255, 255, 0.05);

  }
  .config-section :global(.diag-result--ok) {

    border-color: #4caf7d;

    background: rgba(76, 175, 80, 0.08);

  }
  .config-section :global(.diag-result--fail) {

    border-color: #e05252;

    background: rgba(224, 82, 82, 0.08);

  }
  .config-section :global(.diag-summary) {

    font-weight: 600;

    margin: 0 0 6px;

  }
  .config-section :global(.diag-result--ok .diag-summary) { color: #4caf7d; }
  .config-section :global(.diag-result--fail .diag-summary) { color: #e05252; }
  .config-section :global(.diag-steps) {

    list-style: none;

    padding: 0;

    margin: 0;

    display: flex;

    flex-direction: column;

    gap: 4px;

  }
  .config-section :global(.diag-step) {

    display: grid;

    grid-template-columns: 18px 90px 1fr;

    gap: 6px;

    align-items: start;

    font-size: 0.88em;

    color: var(--text-muted);

  }
  .config-section :global(.diag-step--fail .diag-icon) { color: #e05252; }
  .config-section :global(.diag-step:not(.diag-step--fail) .diag-icon) { color: #4caf7d; }
  .config-section :global(.diag-step-name) {

    font-weight: 600;

    color: var(--text-main);

  }
  .config-section :global(.diag-step-detail) {

    word-break: break-word;

  }
  .config-section :global(.time-select) {

    width: min(180px, 100%);

  }
  @media (max-width: 640px) {
  .config-section :global(.field-row) {

      grid-template-columns: 1fr;

      gap: 6px;

    }
  .config-section :global(.input-action-row) {

      grid-template-columns: minmax(0, 1fr) auto;

      gap: 12px;

    }
  .config-section :global(.field-control--inline) {

      grid-template-columns: 80px 1fr;

    }
  .config-section :global(.choice-grid) {

      grid-template-columns: 1fr;

    }
  .config-section :global(.choice-grid--triple) {

      grid-template-columns: 1fr;

    }
  .config-section :global(.button-row--push) {

      grid-template-columns: 1fr;

    }
  .config-section :global(.topic-card summary) {

      align-items: flex-start;

      flex-direction: column;

    }
  .config-section :global(.topic-card-meta) {

      margin-left: 0;

    }
  }
  .config-section :global(.mqtt-status-row) {

    display: flex;

    align-items: center;

    gap: 10px;

    padding: 10px 14px;

    border-radius: 10px;

    background: rgba(255, 255, 255, 0.05);

    margin-bottom: 14px;

  }
  .config-section :global(.mqtt-dot) {

    width: 10px;

    height: 10px;

    border-radius: 50%;

    flex-shrink: 0;

  }
  .config-section :global(.mqtt-dot--connected) { background: #4caf50; box-shadow: 0 0 6px rgba(76,175,80,0.5); }
  .config-section :global(.mqtt-dot--connecting),
  .config-section :global(.mqtt-dot--backoff) { background: #ff9800; box-shadow: 0 0 6px rgba(255,152,0,0.5); }
  .config-section :global(.mqtt-dot--disconnected) { background: #f44336; }
  .config-section :global(.mqtt-dot--uninitialized),
  .config-section :global(.mqtt-dot--unknown) { background: rgba(200,200,200,0.25); }
  .config-section :global(.mqtt-state-text) {

    flex: 1;

    font-size: 0.9rem;

    color: var(--text-muted);

  }
  .config-section :global(.secondary-sm) {

    padding: 5px 12px;

    font-size: 0.8rem;

    border: 1px solid var(--surface-border);

    border-radius: 8px;

    background: rgba(255,255,255,0.07);

    color: var(--text-main);

    cursor: pointer;

    white-space: nowrap;

  }
  .config-section :global(.secondary-sm:hover) {

    background: rgba(255,255,255,0.12);

  }
  .config-section :global(/* Backup-Modul */

  .backup-group-title) {

    margin: 1.2rem 0 0.3rem;

    font-size: 0.95rem;

    letter-spacing: 0.04em;

    text-transform: uppercase;

    color: var(--text-muted);

  }
</style>
