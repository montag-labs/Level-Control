import { derived, get, writable } from 'svelte/store';
import { APP_STRINGS } from './i18n/appStrings.js';
import { CONFIG_STRINGS } from './i18n/configStrings.js';

const FEATURE_STRINGS = [CONFIG_STRINGS, APP_STRINGS];

export const TRANSLATIONS = {
  de: {
    appTitle: 'Stand Control',
    appTagline: 'Smart Reservoir Monitor',
    updateAvailableTitle: 'Update verfügbar',
    restart: 'Neustart',
    day: 'Tag',
    night: 'Nacht',
    moreModules: 'Weitere Module',
    more: 'Mehr',
    language: 'Sprache',
    modules: {
      dashboard: { label: 'Dashboard', subtitle: 'Live-Messwerte und Netzwerkstatus in einer kompakten Übersicht.' },
      admin: { label: 'Admin', subtitle: 'Geschützte Geräteverwaltung und Systemeinstellungen.' },
      sensor: { label: 'Konfiguration', subtitle: 'Behälterparameter und Sensor-Abtastrate zentral konfigurieren.' },
      wifi: { label: 'WiFi', subtitle: 'WLAN-Zugang und optionale statische Netzwerkdaten verwalten.' },
      mqtt_ha: { label: 'MQTT & HA', subtitle: 'Broker, Discovery und Home-Assistant-Anbindung zentral steuern.' },
      push: { label: 'Push Nachricht', subtitle: 'E-Mail Benachrichtigungen bei Schwellwert, Zeit und Zyklus konfigurieren.' },
      update: { label: 'Update', subtitle: 'OTA aus Releases oder lokales BIN-Upload mit Dateikontrolle durchführen.' },
      backup: { label: 'Sicherung', subtitle: 'Konfiguration und Messverlauf exportieren und importieren.' }
    },
    admin: {
      eyebrow: 'Administration',
      title: 'Admin-Bereich',
      subtitle: 'Geschützte Einstellungen, Wartung und Updates.',
      login: 'Admin anmelden',
      checking: 'Admin-Sitzung wird geprüft …',
      loginRequired: 'Bitte anmelden, um die Administration zu öffnen.',
      loginFailed: 'Admin-Anmeldung fehlgeschlagen.',
      unsavedConfirm: 'Ungespeicherte Änderungen verwerfen?',
      unsavedTitle: 'Ungespeicherte Änderungen',
      discardChanges: 'Verwerfen',
      navigationLabel: 'Admin-Module',
      modules: {
        sensor: 'Konfiguration',
        wifi: 'WiFi',
        mqtt_ha: 'MQTT & HA',
        push: 'Push Nachrichten',
        backup: 'Sicherung',
        password: 'Admin-Passwort',
        update: 'Update'
      },
      passwordEyebrow: 'Zugangsschutz',
      passwordTitle: 'Admin-Passwort ändern',
      passwordSubtitle: 'Ändere das Passwort für alle geschützten Geräteaktionen.',
      newPassword: 'Neues Passwort',
      repeatPassword: 'Passwort wiederholen',
      passwordSave: 'Passwort speichern',
      passwordSaving: 'Wird gespeichert …',
      passwordTooShort: 'Das Passwort muss mindestens 8 Zeichen enthalten.',
      passwordMismatch: 'Die Passwörter stimmen nicht überein.',
      passwordChanged: 'Admin-Passwort wurde geändert.',
      passwordChangeFailed: 'Admin-Passwort konnte nicht geändert werden.'
    },
    restartDialogTitle: 'ESP neu starten?',
    restartDialogMessage: 'Das Gerät wird sofort neu gestartet. Die Weboberfläche ist für kurze Zeit nicht erreichbar.',
    restartDialogConfirm: 'Jetzt neu starten',
    cancel: 'Abbrechen',
    restartFailed: 'Neustart konnte nicht ausgelöst werden.',
    restartSuccess: 'Neustart wurde ausgelöst. Das Gerät ist gleich kurz nicht erreichbar.',
    dashboard: {
      historyTitle: 'Stand Verlauf',
      resetHistory: 'Verlauf löschen',
      distance: 'Aktuelle Distanz',
      level: 'Stand',
      chartAria: 'Verlauf des Standes in Prozent',
      disconnected: 'Verbindung getrennt - Werte werden nicht aktualisiert',
      stale: 'Wert möglicherweise veraltet',
      sensorError: 'Sensorfehler - letzter gültiger Messwert wird angezeigt',
      refill: 'Nachfüllen empfohlen',
      sensorStateLabel: 'Messstatus',
      sensorStateOk: 'OK',
      sensorStateRefill: 'Nachfüllen',
      sensorStateError: 'Sensorfehler',
      sensorStateDisconnected: 'Verbindung unterbrochen',
      sensorStateStale: 'Messwert veraltet',
      period: 'Zeitraum',
      recordStart: 'Start der Aufzeichnung',
      lastHistoryPoint: 'Letzter Verlaufspunkt',
      noHistory: 'Noch keine Historie',
      system: 'System',
      network: 'Netzwerk',
      wifiSignal: 'WiFi Signal',
      uptime: 'Uptime',
      qualityGood: 'Gut',
      qualityMedium: 'Mittel',
      qualityBad: 'Schlecht'
    },
    config: {
      sensorTitle: 'Konfiguration',
      hardware: 'Hardware-Ausführung:',
      laserVersion: 'Laser-Version:',
      offset: 'Offset (cm):',
      height: 'Behälterhöhe (cm):',
      sampleRate: 'Abtastrate:',
      minRateHint: 'Kleinster zulässiger Wert: 5 Sekunden.',
      save: 'Speichern',
      deviceNamePlaceholder: 'Stand',
      pushTitle: 'Push Nachricht',
      sketchNote: 'Offset verschiebt die Referenz der Sensorposition. Positive Werte vergrößern, negative Werte verkleinern den berechneten Füllstand.'
    }
  },
  en: {
    appTitle: 'Level Control',
    appTagline: 'Smart Reservoir Monitor',
    updateAvailableTitle: 'Update available',
    restart: 'Restart',
    day: 'Day',
    night: 'Night',
    moreModules: 'More modules',
    more: 'More',
    language: 'Language',
    modules: {
      dashboard: { label: 'Dashboard', subtitle: 'Live telemetry and network status in one compact overview.' },
      admin: { label: 'Admin', subtitle: 'Protected device administration and system settings.' },
      sensor: { label: 'Configuration', subtitle: 'Configure container parameters and sensor interval in one place.' },
      wifi: { label: 'WiFi', subtitle: 'Manage WiFi access and optional static network settings.' },
      mqtt_ha: { label: 'MQTT & HA', subtitle: 'Control broker, discovery and Home Assistant integration.' },
      push: { label: 'Push Message', subtitle: 'Configure email notifications by threshold, schedule and cycle.' },
      update: { label: 'Update', subtitle: 'Run OTA from releases or local BIN uploads with validation.' },
      backup: { label: 'Backup', subtitle: 'Export and import configuration and measurement history.' }
    },
    admin: {
      eyebrow: 'Administration',
      title: 'Admin area',
      subtitle: 'Protected settings, maintenance and updates.',
      login: 'Admin login',
      checking: 'Checking admin session …',
      loginRequired: 'Please log in to open administration.',
      loginFailed: 'Admin login failed.',
      unsavedConfirm: 'Discard unsaved changes?',
      unsavedTitle: 'Unsaved changes',
      discardChanges: 'Discard',
      navigationLabel: 'Admin modules',
      modules: {
        sensor: 'Configuration',
        wifi: 'WiFi',
        mqtt_ha: 'MQTT & HA',
        push: 'Push messages',
        backup: 'Backup',
        password: 'Admin password',
        update: 'Update'
      },
      passwordEyebrow: 'Access protection',
      passwordTitle: 'Change admin password',
      passwordSubtitle: 'Change the password for all protected device actions.',
      newPassword: 'New password',
      repeatPassword: 'Repeat password',
      passwordSave: 'Save password',
      passwordSaving: 'Saving …',
      passwordTooShort: 'The password must contain at least 8 characters.',
      passwordMismatch: 'The passwords do not match.',
      passwordChanged: 'Admin password changed.',
      passwordChangeFailed: 'Admin password could not be changed.'
    },
    restartDialogTitle: 'Restart ESP?',
    restartDialogMessage: 'The device restarts immediately. The web UI will be unavailable for a short moment.',
    restartDialogConfirm: 'Restart now',
    cancel: 'Cancel',
    restartFailed: 'Could not trigger restart.',
    restartSuccess: 'Restart triggered. Device will be unavailable briefly.',
    dashboard: {
      historyTitle: 'Level History',
      resetHistory: 'Clear history',
      distance: 'Current Distance',
      level: 'Level',
      chartAria: 'Level history in percent',
      disconnected: 'Connection lost - values are not updating',
      stale: 'Value might be stale',
      sensorError: 'Sensor error - showing the last valid measurement',
      refill: 'Refill recommended',
      sensorStateLabel: 'Measurement status',
      sensorStateOk: 'OK',
      sensorStateRefill: 'Refill',
      sensorStateError: 'Sensor error',
      sensorStateDisconnected: 'Connection interrupted',
      sensorStateStale: 'Measurement stale',
      period: 'Period',
      recordStart: 'Recording start',
      lastHistoryPoint: 'Last history point',
      noHistory: 'No history yet',
      system: 'System',
      network: 'Network',
      wifiSignal: 'WiFi Signal',
      uptime: 'Uptime',
      qualityGood: 'Good',
      qualityMedium: 'Medium',
      qualityBad: 'Poor'
    },
    config: {
      sensorTitle: 'Configuration',
      hardware: 'Hardware type:',
      laserVersion: 'Laser version:',
      offset: 'Offset (cm):',
      height: 'Container height (cm):',
      sampleRate: 'Sampling interval:',
      minRateHint: 'Minimum allowed value: 5 seconds.',
      save: 'Save',
      deviceNamePlaceholder: 'Stand',
      pushTitle: 'Push Message',
      sketchNote: 'Offset shifts the reference at sensor position. Positive values increase and negative values reduce the calculated level.'
    }
  }
};

export const SUPPORTED_LANGS = [
  { value: 'de', label: 'Deutsch' },
  { value: 'en', label: 'English' }
];

function lookup(dictionary, path) {
  return path.split('.').reduce((acc, segment) => (acc && acc[segment] !== undefined ? acc[segment] : undefined), dictionary);
}

// Fügt die Texte weiterer Dateien in die Wörterbücher ein (tiefes Zusammenführen).
function mergeDeep(target, source) {
  for (const [key, value] of Object.entries(source)) {
    if (value && typeof value === 'object' && !Array.isArray(value)) {
      target[key] = mergeDeep(target[key] && typeof target[key] === 'object' ? target[key] : {}, value);
    } else {
      target[key] = value;
    }
  }
  return target;
}

for (const extra of FEATURE_STRINGS) {
  for (const lang of Object.keys(extra)) {
    TRANSLATIONS[lang] = mergeDeep(TRANSLATIONS[lang] || {}, extra[lang]);
  }
}

// t('pfad') liefert den Text; t('pfad', { name: 'x' }) ersetzt {name}; t('pfad', 'Ersatz') nutzt den Ersatz bei fehlendem Schlüssel.
// Fehlt ein Schlüssel in der gewählten Sprache, wird der deutsche Text verwendet.
export function createTranslator(lang) {
  const dictionary = TRANSLATIONS[lang] || TRANSLATIONS.de;
  return function t(path, params) {
    let value = lookup(dictionary, path);
    if (value === undefined && dictionary !== TRANSLATIONS.de) {
      value = lookup(TRANSLATIONS.de, path);
    }
    if (value === undefined) {
      return typeof params === 'string' ? params : path;
    }
    if (typeof value === 'string' && params && typeof params === 'object') {
      return value.replace(/\{(\w+)\}/g, (match, key) => (key in params ? String(params[key]) : match));
    }
    return value;
  };
}

// Aktuelle Sprache für Komponenten und Skripte ohne eigenes lang-Prop (App setzt sie bei jedem Wechsel).
//   Komponenten: $translate('update.title')    Skripte: tr('update.title', { name: 'x' })
export const language = writable('de');
export const translate = derived(language, ($language) => createTranslator($language));

export function tr(path, params) {
  return get(translate)(path, params);
}
