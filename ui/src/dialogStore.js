import { writable } from 'svelte/store';
import { tr } from './i18n.js';

export const activeNotice = writable(null);
export const activeConfirmation = writable(null);

let noticeTimer;

function clearNoticeTimer() {
  if (!noticeTimer) {
    return;
  }

  clearTimeout(noticeTimer);
  noticeTimer = undefined;
}

export function showNotice(type, message, options = {}) {
  clearNoticeTimer();

  const duration = options.duration ?? (type === 'success' ? 4200 : 0);
  activeNotice.set({
    type,
    title: options.title || tr(type === 'error' ? 'dialog.error' : 'dialog.success'),
    message,
    duration
  });

  if (duration > 0) {
    noticeTimer = setTimeout(() => {
      activeNotice.set(null);
      noticeTimer = undefined;
    }, duration);
  }
}

export function closeNotice() {
  clearNoticeTimer();
  activeNotice.set(null);
}

export function confirmAction(options) {
  return new Promise((resolve) => {
    activeConfirmation.set({
      title: options.title || tr('dialog.confirmTitle'),
      message: options.message || '',
      confirmLabel: options.confirmLabel || tr('dialog.confirm'),
      cancelLabel: options.cancelLabel || tr('dialog.cancel'),
      tone: options.tone || 'default',
      resolve
    });
  });
}

export function resolveConfirmation(result) {
  let currentDialog;
  activeConfirmation.update((dialog) => {
    currentDialog = dialog;
    return null;
  });

  if (currentDialog?.resolve) {
    currentDialog.resolve(result);
  }
}