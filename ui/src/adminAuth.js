const ADMIN_TOKEN_KEY = 'level-control-admin-token';
import { writable } from 'svelte/store';
import { tr } from './i18n.js';

export const activeAdminLogin = writable(null);
export const adminSessionActive = writable(false);

function getStoredToken() {
  return localStorage.getItem(ADMIN_TOKEN_KEY) || '';
}

function storeToken(token) {
  if (token) {
    localStorage.setItem(ADMIN_TOKEN_KEY, token);
  } else {
    localStorage.removeItem(ADMIN_TOKEN_KEY);
  }
}

async function getAuthStatus() {
  const token = getStoredToken();
  const headers = token ? { 'X-Admin-Token': token } : {};
  const response = await fetch('/api/auth/status', { cache: 'no-store', headers });
  if (!response.ok) {
    throw new Error(tr('auth.statusFailed'));
  }
  return response.json();
}

function describeLoginError(payload, status) {
  switch (payload.error) {
    case 'admin_login_failed':
      return tr('auth.wrongPassword');
    case 'admin_login_locked':
      return tr('auth.locked', { seconds: payload.retryAfterSeconds ?? 60 });
    case 'admin_password_invalid':
      return tr('auth.invalidPassword');
    case 'admin_already_configured':
      return tr('auth.alreadyConfigured');
    default:
      return payload.error || (status === 429 ? tr('auth.tooManyRequests') : tr('auth.failed'));
  }
}

async function authenticate() {
  const status = await getAuthStatus();
  const storedToken = getStoredToken();
  if (storedToken && status.authenticated) {
    adminSessionActive.set(true);
    return storedToken;
  }

  const credentials = await new Promise((resolve, reject) => {
    activeAdminLogin.set({ configured: status.configured, resolve, reject });
  });

  const endpoint = status.configured ? '/api/auth/login' : '/api/auth/setup';
  const response = await fetch(endpoint, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ password: credentials.password })
  });
  const payload = await response.json().catch(() => ({}));
  if (!response.ok || !payload.token) {
    throw new Error(describeLoginError(payload, response.status));
  }

  storeToken(payload.token);
  adminSessionActive.set(true);
  return payload.token;
}

export async function ensureAdminSession() {
  return authenticate();
}

export async function getAdminToken() {
  return authenticate();
}

export async function adminFetch(url, options = {}) {
  const token = await authenticate();
  const headers = new Headers(options.headers || {});
  headers.set('X-Admin-Token', token);
  const response = await fetch(url, { ...options, headers });
  if (response.status === 401) {
    storeToken('');
    throw new Error(tr('auth.sessionExpired'));
  }
  return response;
}

export function clearAdminSession() {
  storeToken('');
  adminSessionActive.set(false);
}

export async function logoutAdmin() {
  await fetch('/api/auth/logout', { method: 'POST' }).catch(() => {});
  clearAdminSession();
}

export async function changeAdminPassword(password) {
  const response = await adminFetch('/api/auth/change', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ password })
  });
  const payload = await response.json().catch(() => ({}));
  if (!response.ok || !payload.token) {
    throw new Error(payload.error || tr('auth.changeFailed'));
  }
  storeToken(payload.token);
  adminSessionActive.set(true);
}
