/* ==========================================================================
   ElectraVote — Frontend Application
   Vanilla JS, no dependencies. Talks to the C++ backend's JSON API.
   Authentication is a HttpOnly session cookie set by the server; the
   frontend never stores passwords and never decides permissions itself —
   it only renders what the server allows.
   ========================================================================== */

(function () {
'use strict';

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const state = {
  user: null,          // {id, username, displayName, role, orgId} from /api/me
  view: 'home',
  params: {},          // route params
  elections: [],
  orgs: [],
  users: [],
  audit: [],
  resultsCache: {},
  selectedCandidate: null,   // candidate pending confirmation
  election: null,            // currently loaded election detail
};

// ---------------------------------------------------------------------------
// API client
// ---------------------------------------------------------------------------

async function api(path, opts) {
  opts = opts || {};
  const init = {
    method: opts.method || 'GET',
    headers: {},
  };
  if (opts.body !== undefined) {
    init.body = JSON.stringify(opts.body);
    init.headers['Content-Type'] = 'application/json';
  }
  const res = await fetch('/api/' + path, init);
  let data = null;
  try { data = await res.json(); } catch (e) { /* non-JSON (e.g. CSV) */ }
  if (!res.ok) {
    const msg = (data && data.error) || ('HTTP ' + res.status);
    const err = new Error(msg);
    err.status = res.status;
    err.data = data;
    throw err;
  }
  return data;
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

function escapeHtml(s) {
  if (s === null || s === undefined) return '';
  return String(s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;').replace(/'/g, '&#039;');
}

function arrowIcon() {
  return '<svg class="direction-arrow" aria-hidden="true" viewBox="0 0 20 20"><path d="M4 10h11M10 5l5 5-5 5"/></svg>';
}

function toast(message, type) {
  const region = document.getElementById('toast-region');
  const el = document.createElement('div');
  el.className = 'toast toast-' + (type || 'info');
  el.setAttribute('role', 'status');
  el.textContent = message;
  region.appendChild(el);
  setTimeout(() => { el.classList.add('toast-out'); }, 3500);
  setTimeout(() => { el.remove(); }, 4200);
}

function fmtStatus(status) {
  const map = { draft: 'Draft', open: 'Open', closed: 'Closed',
                published: 'Published', archived: 'Archived' };
  return map[status] || status;
}

function roleLabel(role) {
  const map = { platform_admin: 'Platform admin', org_admin: 'Org admin',
                observer: 'Observer', voter: 'Voter' };
  return map[role] || role;
}

// ---------------------------------------------------------------------------
// Router (hash-based)
// ---------------------------------------------------------------------------

const routes = [
  { name: 'home',       pattern: /^#?\/?$/ },
  { name: 'login',      pattern: /^#\/login$/ },
  { name: 'register',   pattern: /^#\/register$/ },
  { name: 'elections',  pattern: /^#\/elections$/ },
  { name: 'election',   pattern: /^#\/election\/(\d+)$/, params: ['id'] },
  { name: 'results',    pattern: /^#\/results\/(\d+)$/, params: ['id'] },
  { name: 'admin',      pattern: /^#\/admin$/ },
  { name: 'adminElection', pattern: /^#\/admin\/election\/(\d+)$/, params: ['id'] },
  { name: 'observer',   pattern: /^#\/observer$/ },
  { name: 'settings',   pattern: /^#\/settings$/ },
];

function navigate() {
  const hash = location.hash || '#/';
  for (const r of routes) {
    const m = hash.match(r.pattern);
    if (m) {
      state.view = r.name;
      state.params = {};
      if (r.params) r.params.forEach((p, i) => { state.params[p] = m[i + 1]; });
      state.selectedCandidate = null;
      render();
      window.scrollTo({ top: 0, behavior: 'smooth' });
      return;
    }
  }
  state.view = 'notfound';
  render();
}

window.addEventListener('hashchange', navigate);

function go(path) { location.hash = path; }

// ---------------------------------------------------------------------------
// Shell rendering
// ---------------------------------------------------------------------------

const app = document.getElementById('app');

function navHtml() {
  const u = state.user;
  const homeNav = state.view === 'home';
  const links = [];
  if (u && !homeNav && (u.role === 'org_admin' || u.role === 'platform_admin')) links.push(['#/admin', 'Administration']);
  if (u && !homeNav && u.role === 'observer') links.push(['#/observer', 'Observer view']);
  let html = '<nav class="topnav" aria-label="Main navigation">';
  html += '<a class="brand" href="#/" aria-label="ElectraVote home"><img class="brand-image" src="/assets/electravote-logo-light.png" alt="ElectraVote"></a>';
  if (links.length) html += '<div class="nav-links">';
  for (const [href, label] of links) {
    const route = href.slice(1).split('/')[1] || 'home';
    const active = state.view === route ||
      (route === 'admin' && state.view === 'adminElection') ||
      (route === 'elections' && state.view === 'election');
    html += `<a href="${href}"${active ? ' aria-current="page"' : ''}><span>${label}</span></a>`;
  }
  if (links.length) html += '</div>';
  html += '<div class="nav-user">';
  if (u && !homeNav) {
    html += '<a class="btn nav-home" href="#/" aria-label="Back to home"><svg viewBox="0 0 20 20" aria-hidden="true"><path d="m2.5 9 7.5-6 7.5 6M4.5 8v9h4v-5h3v5h4V8"/></svg><span>Home</span></a>';
    html += `<a class="btn nav-profile-action" href="#/settings" aria-label="Profile: ${escapeHtml(u.displayName)}"${state.view === 'settings' ? ' aria-current="page"' : ''}><svg viewBox="0 0 20 20" aria-hidden="true"><circle cx="10" cy="6.2" r="3.1"/><path d="M3.8 17a6.2 6.2 0 0 1 12.4 0"/></svg><span>Profile</span></a>`;
    html += '<button class="btn btn-link nav-profile-action" id="btn-logout"><svg viewBox="0 0 20 20" aria-hidden="true"><path d="M8 3H4.5A1.5 1.5 0 0 0 3 4.5v11A1.5 1.5 0 0 0 4.5 17H8M11 6l4 4-4 4M7 10h8"/></svg><span>Log out</span></button>';
  } else {
    html += '<a class="btn btn-link nav-signin" href="#/login">Sign in</a>';
    html += `<a class="btn btn-primary nav-start" href="#/register">Get started ${arrowIcon()}</a>`;
  }
  html += '</div></nav>';
  return html;
}

function render() {
  app.innerHTML = navHtml() + '<div id="view"></div>';
  const logout = document.getElementById('btn-logout');
  if (logout) {
    logout.addEventListener('click', async () => {
      try { await api('auth/logout', { method: 'POST' }); } catch (e) { /* ignore */ }
      state.user = null;
      toast('You have been logged out.');
      go('#/');
      render();
    });
  }
  const view = document.getElementById('view');
  const renderers = {
    home: renderHome,
    login: renderLogin,
    register: renderRegister,
    elections: renderElections,
    election: renderElectionDetail,
    results: renderResults,
    admin: renderAdmin,
    adminElection: renderAdminElection,
    observer: renderObserver,
    settings: renderSettings,
    notfound: renderNotFound,
  };
  (renderers[state.view] || renderNotFound)(view);
}

function renderNotFound(view) {
  view.innerHTML = `
    <section class="card">
      <h2>Page not found</h2>
      <p class="muted">The page you requested does not exist.</p>
      <a class="btn btn-primary" href="#/">Back to home</a>
    </section>`;
}

// ---------------------------------------------------------------------------
// Views: home
// ---------------------------------------------------------------------------

async function renderHome(view) {
  const u = state.user;
  let electionsHtml = '';
  // Public discovery: elections of organizations that allow self-registration
  // are listed for logged-in users via /api/elections.
  if (u) {
    electionsHtml = `
      <section class="card">
        <h2>Open elections</h2>
        <div id="home-elections" class="election-list"><div class="empty">Loading…</div></div>
        <div class="btn-row">
          <a class="btn btn-secondary" href="#/elections">My elections ${arrowIcon()}</a>
          ${u.role === 'platform_admin' || u.role === 'org_admin' ? `<a class="btn btn-primary" href="#/admin">Administration ${arrowIcon()}</a>` : ''}
          ${u.role === 'observer' ? `<a class="btn btn-secondary" href="#/observer">Observer view ${arrowIcon()}</a>` : ''}
        </div>
      </section>`;
  }
  view.innerHTML = `
    <header class="hero">
      <div class="hero-inner">
        <p class="hero-kicker"><span class="kicker-dot"></span> THE FUTURE OF DIGITAL DEMOCRACY</p>
        <h1>Every Voice Matters.<br><em>Every Vote Counts.</em></h1>
        <p class="subtitle">Experience a clearer way to participate in democracy. Explore elections, understand your options, and make your voice count in your community.</p>
        <div class="hero-actions">
          ${!u ? `<a class="btn btn-outline btn-lg hero-discover-link" href="#how-it-works">Discover how it works ${arrowIcon()}</a>` : `<a class="btn btn-primary btn-lg" href="#/elections">Explore your elections ${arrowIcon()}</a>`}
        </div>
        <p class="hero-note"><span aria-hidden="true">✓</span> Built for transparency. Designed for participation.</p>
      </div>
      <div class="hero-art" aria-label="Illustration of a verified digital ballot" role="img">
        <div class="art-orbit orbit-one"></div><div class="art-orbit orbit-two"></div>
        <div class="ballot-card"><div class="ballot-top"><img class="ballot-logo" src="/assets/electravote-logo.png" alt="ElectraVote"><span class="ballot-live"><i></i> BALLOT PREVIEW</span></div><div class="ballot-rule"></div><span class="ballot-eyebrow">COMMUNITY ELECTION</span><strong class="ballot-title">Your voice<br>shapes tomorrow.</strong><div class="ballot-option"><span class="option-ring"></span><span><b>Option one</b><small>Your choice matters</small></span></div><div class="ballot-option chosen"><span class="option-ring">✓</span><span><b>Option two</b><small>Selected by you</small></span><span class="selected-tag">SELECTED</span></div><div class="ballot-submit"><span>Submit your ballot</span>${arrowIcon()}</div><div class="ballot-foot"><span>● Sample election interface</span><span>1 of 1</span></div></div>
        <div class="float-chip chip-verified"><span>✓</span><div><b>Vote verified</b><small>Confirmation received</small></div></div><div class="float-chip chip-participate"><span class="chip-bars">▥</span><div><b>Every voice counts</b><small>Participation made simple</small></div></div>
        <span class="art-spark spark-a">✳</span><span class="art-spark spark-b">+</span>
      </div>
    </header>
    <section class="trust-strip" aria-label="ElectraVote principles"><span>SECURE BY DESIGN</span><i></i><span>TRANSPARENT PROCESS</span><i></i><span>OPEN TO EVERY VOICE</span></section>
    <section class="feature-section" id="how-it-works"><div class="section-heading"><span class="section-kicker">A better way to decide</span><h2>Democracy, made <em>accessible.</em></h2><p>Everything your organization needs to bring people into the decisions that matter.</p></div><div class="feature-grid">
      <article class="feature-card"><span class="feature-icon icon-lock" aria-hidden="true"><svg viewBox="0 0 24 24"><rect x="4" y="10" width="16" height="11" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3m-4 5v2"/></svg></span><h3>Secure voting</h3><p>Clear eligibility and a confirmation step help protect the integrity of each ballot.</p></article>
      <article class="feature-card"><span class="feature-icon icon-eye" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12Z"/><circle cx="12" cy="12" r="3"/></svg></span><h3>Transparent results</h3><p>Published outcomes and election records make the process easier to understand.</p></article>
      <article class="feature-card"><span class="feature-icon icon-people" aria-hidden="true"><svg viewBox="0 0 24 24"><circle cx="9" cy="8" r="3"/><path d="M3 20v-2a6 6 0 0 1 12 0v2m2-9a3 3 0 1 0-1-5.8M18 14a5 5 0 0 1 3 4.6V20"/></svg></span><h3>Accessible participation</h3><p>Thoughtful, responsive experiences help eligible voters take part wherever they are.</p></article>
    </div></section>
    ${electionsHtml}
    <section class="closing-cta"><div><span class="section-kicker">Your community. Your choice.</span><h2>Make your voice part of the decision.</h2></div><a class="btn btn-primary" href="${u ? '#/elections' : '#/register'}">${u ? 'View elections' : 'Create your account'} ${arrowIcon()}</a></section>
    <footer class="site-footer"><a class="footer-brand" href="#/" aria-label="ElectraVote home"><img class="footer-brand-image" src="/assets/electravote-logo.png" alt="ElectraVote"></a><span>Secure. Transparent. Democratic.</span><span>For community and organizational elections.</span></footer>`;
  if (u) {
    try {
      const data = await api('elections');
      const box = document.getElementById('home-elections');
      const open = (data.elections || []).filter(e => e.status === 'open');
      if (open.length === 0) {
        box.innerHTML = '<div class="empty">No elections are open for voting right now.</div>';
      } else {
        box.innerHTML = open.map(e => electionRowHtml(e, state.user)).join('');
        bindElectionRows(box);
      }
    } catch (e) { /* silent on home */ }
  }
}

function electionRowHtml(e, user) {
  const badgeClass = { open: 'success', draft: 'muted-badge', closed: 'warn',
                       published: 'info', archived: 'muted-badge' }[e.status] || 'muted-badge';
  let voterInfo = '';
  if (user && user.role === 'voter') {
    if (e.hasVoted) voterInfo = '<span class="pill pill-info">You have voted</span>';
    else if (e.status === 'open') voterInfo = e.eligible
      ? '<span class="pill pill-success">Eligible</span>'
      : '<span class="pill pill-muted">Not on the voter list</span>';
  }
  const link = (user && (user.role === 'platform_admin' || user.role === 'org_admin'))
    ? `#/admin/election/${e.id}` : `#/election/${e.id}`;
  return `
    <div class="election-row">
      <div class="election-row-main">
        <a href="${link}" class="election-title">${escapeHtml(e.name)}</a>
        <div class="muted small">Organization #${e.orgId} · created ${escapeHtml((e.createdAt || '').slice(0, 10))}</div>
      </div>
      <div class="election-row-side">
        <span class="pill pill-${badgeClass}">${fmtStatus(e.status)}</span>
        ${voterInfo}
        <a class="btn btn-link" href="#/results/${e.id}">Results</a>
      </div>
    </div>`;
}

function bindElectionRows(box) {
  box.querySelectorAll('a.election-title').forEach(a => {
    a.addEventListener('click', () => go(a.getAttribute('href')));
  });
}

// ---------------------------------------------------------------------------
// Views: login / register
// ---------------------------------------------------------------------------

function renderLogin(view) {
  view.innerHTML = `
    <section class="login-layout" aria-labelledby="login-title">
      <aside class="login-aside">
        <img class="login-photo" src="/assets/vote-teal-studio-backdrop.png" alt="A voter holding a sign that says Vote against a teal studio backdrop">
        <span class="login-eyebrow">ELECTRAVOTE · PARTICIPANT ACCESS</span>
        <div class="login-aside-copy">
          <h2>Your voice matters.</h2>
        </div>
      </aside>
      <div class="login-panel">
        <header class="login-heading">
          <span class="login-kicker">ACCOUNT ACCESS</span>
          <h1 id="login-title">Sign in</h1>
          <p>Use your username and password to continue.</p>
        </header>
        <form id="form-login" class="stack-form login-form">
          <label for="login-username">Username
            <input id="login-username" type="text" name="username" autocomplete="username" placeholder="Enter your username" required>
          </label>
          <label for="login-password">Password
            <input id="login-password" type="password" name="password" autocomplete="current-password" placeholder="Enter your password" required>
          </label>
          <div class="error-box" id="login-error" role="alert" aria-live="polite" hidden></div>
          <button type="submit" class="btn btn-primary login-submit">Sign in</button>
          <p class="login-register-prompt">New to ElectraVote? <a href="#/register">Create an account ${arrowIcon()}</a></p>
        </form>
      </div>
    </section>`;
  const form = document.getElementById('form-login');
  form.addEventListener('submit', async (ev) => {
    ev.preventDefault();
    const fd = new FormData(form);
    const errBox = document.getElementById('login-error');
    errBox.hidden = true;
    try {
      const r = await api('auth/login', {
        method: 'POST',
        body: { username: fd.get('username'), password: fd.get('password') }
      });
      state.user = r.user;
      toast('Welcome, ' + r.user.displayName + '!');
      go('#/elections');
    } catch (err) {
      errBox.textContent = err.message;
      errBox.hidden = false;
    }
  });
}

function renderRegister(view) {
  view.innerHTML = `
    <section class="login-layout registration-layout" aria-labelledby="register-title">
      <aside class="login-aside">
        <img class="login-photo" src="/assets/vote-teal-studio-backdrop.png" alt="">
        <span class="login-eyebrow">ELECTRAVOTE · PARTICIPANT ACCESS</span>
        <div class="login-aside-copy"><h2>Your voice matters.</h2></div>
      </aside>
      <div class="login-panel">
        <header class="login-heading">
          <span class="login-kicker">ACCOUNT SETUP</span>
          <h1 id="register-title">Create your account</h1>
          <p>Set up your details to take part in community elections.</p>
        </header>
        <form id="form-register" class="stack-form login-form registration-form">
          <label for="register-username">Username
            <input id="register-username" type="text" name="username" autocomplete="username" minlength="3" maxlength="32" placeholder="Choose a username" required>
          </label>
          <label for="register-display-name"><span class="register-label-row"><span>Display name</span><span class="register-optional">Optional</span></span>
            <input id="register-display-name" type="text" name="displayName" maxlength="100" autocomplete="name" placeholder="How your name will appear">
          </label>
          <label for="register-password"><span class="register-label-row"><span>Password</span><span class="register-field-hint">At least 8 characters</span></span>
            <input id="register-password" type="password" name="password" minlength="8" autocomplete="new-password" placeholder="Create a password" required>
          </label>
          <label for="register-confirm-password">Confirm password
            <input id="register-confirm-password" type="password" name="confirmPassword" minlength="8" autocomplete="new-password" placeholder="Enter your password again" required>
          </label>
          <div class="error-box" id="reg-error" role="alert" aria-live="polite" hidden></div>
          <button type="submit" class="btn btn-primary login-submit">Create account</button>
          <p class="login-register-prompt">Already have an account? <a href="#/login">Sign in ${arrowIcon()}</a></p>
        </form>
      </div>
    </section>`;
  const form = document.getElementById('form-register');
  form.addEventListener('submit', async (ev) => {
    ev.preventDefault();
    const fd = new FormData(form);
    const errBox = document.getElementById('reg-error');
    errBox.hidden = true;
    const password = fd.get('password');
    const confirmPassword = fd.get('confirmPassword');
    const confirmInput = document.getElementById('register-confirm-password');
    if (password !== confirmPassword) {
      errBox.textContent = 'The passwords do not match. Please check both fields.';
      errBox.hidden = false;
      confirmInput.setAttribute('aria-invalid', 'true');
      confirmInput.focus();
      return;
    }
    confirmInput.removeAttribute('aria-invalid');
    try {
      await api('auth/register', {
        method: 'POST',
        body: {
          username: fd.get('username'),
          displayName: fd.get('displayName') || fd.get('username'),
          password,
        }
      });
      toast('Account created. Please log in.');
      go('#/login');
    } catch (err) {
      errBox.textContent = err.message;
      errBox.hidden = false;
    }
  });
}

// ---------------------------------------------------------------------------
// Views: elections list (voter dashboard)
// ---------------------------------------------------------------------------

async function renderElections(view) {
  if (!state.user) { go('#/login'); return; }
  view.innerHTML = `
    <section class="card">
      <header class="page-head-inline">
        <h2>My elections</h2>
      </header>
      <p class="muted">Elections you can view. Only elections that are
      <strong>open</strong> accept ballots, and results appear only after they
      are published by the administrators.</p>
      <div id="election-list" class="election-list"><div class="empty">Loading…</div></div>
    </section>`;
  try {
    const data = await api('elections');
    const box = document.getElementById('election-list');
    const list = data.elections || [];
    if (list.length === 0) {
      box.innerHTML = '<div class="empty">No elections are available for your account yet. You will see an election here once an organization adds you to its voter list.</div>';
      return;
    }
    box.innerHTML = list.map(e => electionRowHtml(e, state.user)).join('');
    bindElectionRows(box);
  } catch (err) {
    document.getElementById('election-list').innerHTML =
      `<div class="empty">Failed to load: ${escapeHtml(err.message)}</div>`;
  }
}

// ---------------------------------------------------------------------------
// Views: election detail + ballot
// ---------------------------------------------------------------------------

async function renderElectionDetail(view) {
  if (!state.user) { go('#/login'); return; }
  view.innerHTML = '<section class="card"><div class="empty">Loading…</div></section>';
  let e;
  try {
    e = await api('elections/' + state.params.id);
  } catch (err) {
    view.innerHTML = `<section class="card"><h2>Election unavailable</h2>
      <p class="muted">${escapeHtml(err.message)}</p>
      <a class="btn btn-secondary" href="#/elections">← Back to elections</a></section>`;
    return;
  }
  state.election = e;
  const isVoter = state.user.role === 'voter';
  const canVoteNow = isVoter && e.status === 'open' && e.eligible && !e.hasVoted;

  const statusPill = `<span class="pill pill-${{open:'success',closed:'warn',published:'info',draft:'muted-badge',archived:'muted-badge'}[e.status]}">${fmtStatus(e.status)}</span>`;

  let ballotHtml = '';
  if (isVoter) {
    if (e.hasVoted) {
      ballotHtml = `<div class="banner banner-info">✓ You have already voted in this election. Your ballot has been recorded and can no longer be changed.</div>`;
    } else if (e.status === 'open' && !e.eligible) {
      ballotHtml = `<div class="banner banner-warn">You are not on the voter list for this election.</div>`;
    } else if (e.status === 'open' && e.eligible) {
      ballotHtml = `
        <h3>Cast your vote</h3>
        <p class="muted">Select one option. No option is preselected. You will be asked to confirm before your ballot is recorded.</p>
        <div class="candidate-grid" id="ballot-grid" role="radiogroup" aria-label="Ballot options"></div>
        <div class="btn-row">
          <button class="btn btn-primary" id="btn-cast" disabled>Cast vote</button>
        </div>
        <div class="error-box" id="vote-error" hidden></div>`;
    } else if (e.status === 'draft') {
      ballotHtml = `<div class="banner banner-muted">This election is being prepared and is not accepting ballots yet.</div>`;
    } else {
      ballotHtml = `<div class="banner banner-muted">Voting has ended for this election.</div>`;
    }
  }

  const cands = e.candidates || [];
  const candList = cands.length === 0
    ? '<div class="empty">No ballot options have been published yet.</div>'
    : cands.map(c => `
      <div class="candidate-card" data-cid="${c.id}" role="radio" aria-checked="false" tabindex="0">
        <div class="candidate-name">${escapeHtml(c.name)}</div>
        ${c.party ? `<div class="candidate-party">${escapeHtml(c.party)}</div>` : ''}
        ${c.description ? `<div class="candidate-desc">${escapeHtml(c.description)}</div>` : ''}
      </div>`).join('');

  view.innerHTML = `
    <section class="card">
      <header class="page-head-inline">
        <h2>${escapeHtml(e.name)}</h2>
        ${statusPill}
      </header>
      <p class="muted">${escapeHtml(e.description || '')}</p>
      <div class="meta-grid">
        <div><span class="meta-label">Voting window</span>
          ${e.startAt || e.endAt ? `${escapeHtml(e.startAt || 'now')} → ${escapeHtml(e.endAt || 'no end')}` : 'Not restricted'}</div>
        <div><span class="meta-label">Participation</span>
          ${e.participationCount >= 0 ? e.participationCount + ' ballots so far' : 'Not public while voting is active'}</div>
        <div><span class="meta-label">Results</span>
          ${e.status === 'published' || e.status === 'archived' ? '<a href="#/results/' + e.id + '">Published results ' + arrowIcon() + '</a>' : 'Available after publication'}</div>
      </div>
      <h3>Ballot options</h3>
      <div class="candidate-grid static">${candList}</div>
      ${ballotHtml}
    </section>`;

  if (canVoteNow) {
    const grid = document.getElementById('ballot-grid');
    const staticGrid = view.querySelector('.candidate-grid.static');
    // Move candidates into the selectable grid.
    if (staticGrid) grid.innerHTML = staticGrid.innerHTML;
    const castBtn = document.getElementById('btn-cast');
    const select = (card) => {
      grid.querySelectorAll('.candidate-card').forEach(el => {
        el.classList.remove('selected');
        el.setAttribute('aria-checked', 'false');
      });
      card.classList.add('selected');
      card.setAttribute('aria-checked', 'true');
      state.selectedCandidate = Number(card.getAttribute('data-cid'));
      castBtn.disabled = false;
      toast('Selected: ' + card.querySelector('.candidate-name').textContent + '. Press “Cast vote” to confirm.');
    };
    grid.querySelectorAll('.candidate-card').forEach(card => {
      card.addEventListener('click', () => select(card));
      card.addEventListener('keydown', (ev) => {
        if (ev.key === ' ' || ev.key === 'Enter') { ev.preventDefault(); select(card); }
      });
    });
    castBtn.addEventListener('click', async () => {
      if (!state.selectedCandidate) return;
      const choiceName = grid.querySelector('.candidate-card.selected .candidate-name').textContent;
      if (!window.confirm('Confirm your vote for “' + choiceName + '”?\n\nThis cannot be undone and cannot be changed afterwards.')) {
        return;
      }
      castBtn.disabled = true;
      try {
        const r = await api('elections/' + e.id + '/vote', {
          method: 'POST',
          body: { candidateId: state.selectedCandidate }
        });
        view.innerHTML = `
          <section class="card narrow center">
            <div class="big-icon">✅</div>
            <h2>Your vote has been recorded</h2>
            <p>Thank you for participating. Your ballot is anonymous to the
            application: no record links your account to your choice.</p>
            <div class="receipt-box">
              <span class="meta-label">Receipt code</span>
              <code id="receipt-code">${escapeHtml(r.receipt)}</code>
            </div>
            <p class="muted small">Save this receipt code. It confirms your ballot
            was accepted but does not reveal your choice.</p>
            <div class="btn-row center">
              <a class="btn btn-secondary" href="#/elections">Back to elections</a>
              <a class="btn btn-link" href="#/results/${e.id}">Results</a>
            </div>
          </section>`;
      } catch (err) {
        const errBox = document.getElementById('vote-error');
        errBox.textContent = err.message;
        errBox.hidden = false;
        castBtn.disabled = false;
      }
    });
  }
}

// ---------------------------------------------------------------------------
// Views: results
// ---------------------------------------------------------------------------

async function renderResults(view) {
  view.innerHTML = '<section class="card"><div class="empty">Loading…</div></section>';
  let r;
  try {
    r = await api('elections/' + state.params.id + '/results');
  } catch (err) {
    view.innerHTML = `
      <section class="card narrow">
        <h2>Results</h2>
        <div class="banner banner-muted">${escapeHtml(err.message)}</div>
        <p class="muted small">Vote totals are hidden until the administrators
        publish the results. This prevents influencing voters while the election
        is running.</p>
        <a class="btn btn-secondary" href="#/elections">← Back</a>
      </section>`;
    return;
  }
  const total = r.results.reduce((s, c) => s + c.votes, 0);
  const rows = r.results.map(c => {
    const pct = total > 0 ? (c.votes * 100.0 / total) : 0;
    return `
      <tr>
        <td>${escapeHtml(c.name)}</td>
        <td>${escapeHtml(c.party || '')}</td>
        <td>${c.votes}</td>
        <td>${pct.toFixed(1)}%
          <div class="vote-bar"><span style="width:${pct.toFixed(1)}%"></span></div>
        </td>
      </tr>`;
  }).join('');
  view.innerHTML = `
    <section class="card">
      <header class="page-head-inline">
        <h2>Results — ${escapeHtml(r.electionName)}</h2>
        <span class="pill pill-${r.published ? 'info' : 'warn'}">${r.published ? 'Published' : 'Live (configured)'}</span>
      </header>
      <div class="meta-grid">
        <div><span class="meta-label">Total ballots</span>${r.totalBallots}</div>
        <div><span class="meta-label">Participants</span>${r.totalParticipations}</div>
        <div><span class="meta-label">Integrity check</span>${r.integrityOk ? '✓ ballots match participants' : '⚠ mismatch detected'}</div>
      </div>
      <div class="table-wrap">
        <table>
          <thead><tr><th>Option</th><th>Party</th><th>Votes</th><th>Share</th></tr></thead>
          <tbody>${rows}</tbody>
        </table>
      </div>
      <div class="btn-row">
        <a class="btn btn-secondary" href="/api/elections/${r.electionId}/results.csv">Download CSV report</a>
        <a class="btn btn-link" href="#/elections">← Back</a>
      </div>
    </section>`;
}

// ---------------------------------------------------------------------------
// Views: admin dashboard
// ---------------------------------------------------------------------------

async function renderAdmin(view) {
  if (!state.user || (state.user.role !== 'platform_admin' && state.user.role !== 'org_admin')) {
    go('#/elections'); return;
  }
  view.innerHTML = `
    <section class="card">
      <header class="page-head-inline"><h2>Administration</h2></header>
      <nav class="tabs" role="tablist">
        <button class="tab active" data-tab="elections" role="tab">Elections</button>
        ${state.user.role === 'platform_admin' ? '<button class="tab" data-tab="orgs" role="tab">Organizations</button>' : ''}
        <button class="tab" data-tab="users" role="tab">Users</button>
        <button class="tab" data-tab="audit" role="tab">Audit log</button>
      </nav>
      <div id="tab-elections" class="tab-panel active">
        <div class="btn-row">
          <button class="btn btn-primary" id="btn-new-election">+ New election</button>
        </div>
        <form id="form-new-election" class="stack-form panel" hidden>
          ${state.user.role === 'platform_admin' ? `
          <label>Organization
            <select name="orgId" id="ne-org" required></select>
          </label>` : ''}
          <label>Election name
            <input type="text" name="name" maxlength="200" required>
          </label>
          <label>Description
            <textarea name="description" rows="2" maxlength="2000"></textarea>
          </label>
          <label class="check-label">
            <input type="checkbox" name="showLiveResults">
            Show live vote totals while the election is open (deliberate configuration)
          </label>
          <div class="form-row">
            <label>Start (ISO UTC, optional)<input type="text" name="startAt" placeholder="2026-01-01T08:00:00Z"></label>
            <label>End (ISO UTC, optional)<input type="text" name="endAt" placeholder="2026-01-07T20:00:00Z"></label>
          </div>
          <div class="btn-row">
            <button type="submit" class="btn btn-primary">Create election</button>
            <button type="button" class="btn btn-link" id="ne-cancel">Cancel</button>
          </div>
          <div class="error-box" id="ne-error" hidden></div>
        </form>
        <div id="admin-election-list" class="election-list"><div class="empty">Loading…</div></div>
      </div>
      ${state.user.role === 'platform_admin' ? `
      <div id="tab-orgs" class="tab-panel">
        <div class="btn-row"><button class="btn btn-primary" id="btn-new-org">+ New organization</button></div>
        <form id="form-new-org" class="stack-form panel" hidden>
          <label>Name<input type="text" name="name" maxlength="100" required></label>
          <label>Slug (URL-safe identifier)<input type="text" name="slug" maxlength="32" required></label>
          <label class="check-label"><input type="checkbox" name="allowSelfRegistration" checked>
            Allow public self-registration into this organization</label>
          <div class="error-box" id="no-error" hidden></div>
          <div class="btn-row"><button type="submit" class="btn btn-primary">Create</button>
          <button type="button" class="btn btn-link" id="no-cancel">Cancel</button></div>
        </form>
        <div id="org-list" class="table-wrap"><div class="empty">Loading…</div></div>
      </div>` : ''}
      <div id="tab-users" class="tab-panel">
        <div class="btn-row"><button class="btn btn-primary" id="btn-new-user">+ New user</button></div>
        <form id="form-new-user" class="stack-form panel" hidden>
          <label>Username<input type="text" name="username" maxlength="32" required></label>
          <label>Display name<input type="text" name="displayName" maxlength="100"></label>
          <label>Role
            <select name="role">
              <option value="voter">Voter</option>
              <option value="org_admin">Organization admin</option>
              <option value="observer">Observer</option>
              ${state.user.role === 'platform_admin' ? '<option value="platform_admin">Platform admin</option>' : ''}
            </select>
          </label>
          ${state.user.role === 'platform_admin' ? `
          <label>Organization
            <select name="orgId" id="nu-org"></select>
          </label>` : ''}
          <label>Password<input type="password" name="password" minlength="8" required></label>
          <div class="error-box" id="nu-error" hidden></div>
          <div class="btn-row"><button type="submit" class="btn btn-primary">Create user</button>
          <button type="button" class="btn btn-link" id="nu-cancel">Cancel</button></div>
        </form>
        <div id="user-list" class="table-wrap"><div class="empty">Loading…</div></div>
      </div>
      <div id="tab-audit" class="tab-panel">
        <div class="btn-row"><button class="btn btn-secondary" id="btn-refresh-audit">Refresh</button></div>
        <div id="audit-list" class="table-wrap"><div class="empty">Loading…</div></div>
      </div>
    </section>`;

  bindTabs(view);

  // ---- tabs helpers ----
  function bindTabs(view) {
    view.querySelectorAll('.tab').forEach(tab => {
      tab.addEventListener('click', () => {
        view.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
        view.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
        tab.classList.add('active');
        view.querySelector('#tab-' + tab.getAttribute('data-tab')).classList.add('active');
      });
    });
  }

  // ---- org dropdowns (platform admin) ----
  async function loadOrgSelects() {
    if (state.user.role !== 'platform_admin') return;
    const data = await api('orgs');
    state.orgs = data.orgs || [];
    for (const selId of ['ne-org', 'nu-org']) {
      const sel = document.getElementById(selId);
      if (sel) {
        sel.innerHTML = state.orgs.map(o =>
          `<option value="${o.id}">${escapeHtml(o.name)} (${escapeHtml(o.slug)})</option>`).join('');
      }
    }
  }
  await loadOrgSelects();

  // ---- elections list ----
  const listEl = document.getElementById('admin-election-list');
  async function loadElections() {
    try {
      const data = await api('elections');
      const list = (data.elections || []).filter(e =>
        state.user.role === 'platform_admin' || e.orgId === state.user.orgId);
      listEl.innerHTML = list.length === 0
        ? '<div class="empty">No elections yet. Create one to get started.</div>'
        : list.map(e => `
          <div class="election-row">
            <div class="election-row-main">
              <a href="#/admin/election/${e.id}" class="election-title">${escapeHtml(e.name)}</a>
              <div class="muted small">Organization #${e.orgId}</div>
            </div>
            <div class="election-row-side">
              <span class="pill pill-${{open:'success',closed:'warn',published:'info',draft:'muted-badge',archived:'muted-badge'}[e.status]}">${fmtStatus(e.status)}</span>
              <a class="btn btn-secondary" href="#/admin/election/${e.id}">Manage</a>
            </div>
          </div>`).join('');
    } catch (err) {
      listEl.innerHTML = `<div class="empty">${escapeHtml(err.message)}</div>`;
    }
  }
  await loadElections();

  // ---- new election ----
  const neForm = document.getElementById('form-new-election');
  document.getElementById('btn-new-election').addEventListener('click', () => {
    neForm.hidden = !neForm.hidden;
  });
  document.getElementById('ne-cancel').addEventListener('click', () => { neForm.hidden = true; });
  neForm.addEventListener('submit', async (ev) => {
    ev.preventDefault();
    const fd = new FormData(neForm);
    const errBox = document.getElementById('ne-error');
    errBox.hidden = true;
    const body = {
      name: fd.get('name'),
      description: fd.get('description') || '',
      showLiveResults: fd.get('showLiveResults') === 'on',
      startAt: (fd.get('startAt') || '').trim(),
      endAt: (fd.get('endAt') || '').trim(),
    };
    if (state.user.role === 'platform_admin') body.orgId = Number(fd.get('orgId'));
    else body.orgId = state.user.orgId;
    try {
      const r = await api('elections', { method: 'POST', body });
      toast('Election created.');
      neForm.reset(); neForm.hidden = true;
      go('#/admin/election/' + r.election.id);
    } catch (err) {
      errBox.textContent = err.message; errBox.hidden = false;
    }
  });

  // ---- organizations (platform admin) ----
  if (state.user.role === 'platform_admin') {
    const orgList = document.getElementById('org-list');
    const noForm = document.getElementById('form-new-org');
    document.getElementById('btn-new-org').addEventListener('click', () => { noForm.hidden = !noForm.hidden; });
    document.getElementById('no-cancel').addEventListener('click', () => { noForm.hidden = true; });
    noForm.addEventListener('submit', async (ev) => {
      ev.preventDefault();
      const fd = new FormData(noForm);
      const errBox = document.getElementById('no-error');
      errBox.hidden = true;
      try {
        await api('orgs', {
          method: 'POST',
          body: {
            name: fd.get('name'), slug: fd.get('slug'),
            allowSelfRegistration: fd.get('allowSelfRegistration') === 'on'
          }
        });
        toast('Organization created.');
        noForm.reset(); noForm.hidden = true;
        await loadOrgSelects(); await loadOrgs();
      } catch (err) {
        errBox.textContent = err.message; errBox.hidden = false;
      }
    });
    async function loadOrgs() {
      const data = await api('orgs');
      const orgs = data.orgs || [];
      orgList.innerHTML = orgs.length === 0 ? '<div class="empty">No organizations yet.</div>' :
        `<table><thead><tr><th>ID</th><th>Name</th><th>Slug</th><th>Self-registration</th></tr></thead><tbody>` +
        orgs.map(o => `<tr><td>${o.id}</td><td>${escapeHtml(o.name)}</td><td>${escapeHtml(o.slug)}</td><td>${o.allowSelfRegistration ? 'Yes' : 'No'}</td></tr>`).join('') +
        '</tbody></table>';
    }
    await loadOrgs();
  }

  // ---- users ----
  const userList = document.getElementById('user-list');
  const nuForm = document.getElementById('form-new-user');
  document.getElementById('btn-new-user').addEventListener('click', () => { nuForm.hidden = !nuForm.hidden; });
  document.getElementById('nu-cancel').addEventListener('click', () => { nuForm.hidden = true; });
  nuForm.addEventListener('submit', async (ev) => {
    ev.preventDefault();
    const fd = new FormData(nuForm);
    const errBox = document.getElementById('nu-error');
    errBox.hidden = true;
    const body = {
      username: fd.get('username'),
      displayName: fd.get('displayName') || fd.get('username'),
      role: fd.get('role'),
      password: fd.get('password'),
    };
    if (state.user.role === 'platform_admin') body.orgId = Number(fd.get('orgId') || 0);
    try {
      await api('users', { method: 'POST', body });
      toast('User created.');
      nuForm.reset(); nuForm.hidden = true;
      await loadUsers();
    } catch (err) {
      errBox.textContent = err.message; errBox.hidden = false;
    }
  });
  async function loadUsers() {
    try {
      const data = await api('users');
      const users = data.users || [];
      userList.innerHTML = users.length === 0 ? '<div class="empty">No users.</div>' :
        `<table><thead><tr><th>ID</th><th>Username</th><th>Display name</th><th>Role</th><th>Org</th><th>Status</th><th></th></tr></thead><tbody>` +
        users.map(u => `<tr>
          <td>${u.id}</td><td>${escapeHtml(u.username)}</td><td>${escapeHtml(u.displayName)}</td>
          <td>${roleLabel(u.role)}</td><td>${u.orgId || '—'}</td>
          <td>${u.disabled ? '<span class="pill pill-warn">Disabled</span>' : '<span class="pill pill-success">Active</span>'}</td>
          <td>${u.id !== state.user.id ? `<button class="btn btn-link btn-danger" data-disable="${u.id}" data-name="${escapeHtml(u.username)}">${u.disabled ? 'Enable' : 'Disable'}</button>` : ''}</td>
        </tr>`).join('') + '</tbody></table>';
      userList.querySelectorAll('[data-disable]').forEach(btn => {
        btn.addEventListener('click', async () => {
          const disable = btn.textContent.trim() === 'Disable';
          if (!window.confirm((disable ? 'Disable' : 'Enable') + ' account ' + btn.getAttribute('data-name') + '?')) return;
          try {
            await api('users/' + btn.getAttribute('data-disable') + '/disable', {
              method: 'POST', body: { disabled: disable }
            });
            await loadUsers();
          } catch (err) { toast(err.message, 'error'); }
        });
      });
    } catch (err) {
      userList.innerHTML = `<div class="empty">${escapeHtml(err.message)}</div>`;
    }
  }
  await loadUsers();

  // ---- audit ----
  const auditList = document.getElementById('audit-list');
  async function loadAudit() {
    try {
      const data = await api('audit');
      const entries = data.entries || [];
      auditList.innerHTML = entries.length === 0 ? '<div class="empty">No audit events yet.</div>' :
        `<table><thead><tr><th>Time</th><th>Actor</th><th>Action</th><th>Election</th><th>Detail</th></tr></thead><tbody>` +
        entries.map(e => `<tr><td class="mono">${escapeHtml(e.ts)}</td><td>${escapeHtml(e.actor)}</td><td>${escapeHtml(e.action)}</td><td>${e.electionId || '—'}</td><td>${escapeHtml(e.detail || '')}</td></tr>`).join('') +
        '</tbody></table>';
    } catch (err) {
      auditList.innerHTML = `<div class="empty">${escapeHtml(err.message)}</div>`;
    }
  }
  document.getElementById('btn-refresh-audit').addEventListener('click', loadAudit);
  await loadAudit();
}

// ---------------------------------------------------------------------------
// Views: admin election management
// ---------------------------------------------------------------------------

async function renderAdminElection(view) {
  if (!state.user || (state.user.role !== 'platform_admin' && state.user.role !== 'org_admin')) {
    go('#/elections'); return;
  }
  view.innerHTML = '<section class="card"><div class="empty">Loading…</div></section>';
  let e;
  try {
    e = await api('elections/' + state.params.id);
    if (!(state.user.role === 'platform_admin' || e.orgId === state.user.orgId)) {
      throw new Error('You do not administer this election');
    }
  } catch (err) {
    view.innerHTML = `<section class="card"><h2>Election unavailable</h2>
      <p class="muted">${escapeHtml(err.message)}</p><a class="btn btn-secondary" href="#/admin">← Back</a></section>`;
    return;
  }

  const lifecycleButtons = [];
  if (e.status === 'draft') lifecycleButtons.push(['open', 'Open election', 'btn-primary', 'Open the election and accept ballots?']);
  if (e.status === 'open') lifecycleButtons.push(['closed', 'Close election', 'btn-danger', 'Close the election? No further ballots will be accepted.']);
  if (e.status === 'closed') lifecycleButtons.push(['published', 'Publish results', 'btn-primary', 'Run the tally and publish the results? This makes them publicly visible.']);
  if (e.status === 'published') lifecycleButtons.push(['archived', 'Archive election', 'btn-secondary', 'Archive this election?']);

  view.innerHTML = `
    <section class="card">
      <header class="page-head-inline">
        <h2>${escapeHtml(e.name)}</h2>
        <span class="pill pill-${{open:'success',closed:'warn',published:'info',draft:'muted-badge',archived:'muted-badge'}[e.status]}">${fmtStatus(e.status)}</span>
        <a class="btn btn-link" href="#/admin">← All elections</a>
      </header>
      <p class="muted">${escapeHtml(e.description || '')}</p>

      <nav class="tabs" role="tablist">
        <button class="tab active" data-tab="lifecycle" role="tab">Lifecycle</button>
        <button class="tab" data-tab="config" role="tab">Configuration</button>
        <button class="tab" data-tab="candidates" role="tab">Candidates</button>
        <button class="tab" data-tab="eligibility" role="tab">Voter list</button>
        <button class="tab" data-tab="admresults" role="tab">Results</button>
      </nav>

      <div id="tab-lifecycle" class="tab-panel active">
        <h3>Lifecycle</h3>
        <p class="muted small">Lifecycle: draft → open → closed → published → archived.
        Configuration can only be edited in draft. Results can only be published
        from a closed election after a successful integrity check.</p>
        <div class="btn-row" id="lifecycle-buttons">
          ${lifecycleButtons.map(([s, label, cls, confirmMsg]) =>
            `<button class="btn ${cls}" data-transition="${s}" data-confirm="${escapeHtml(confirmMsg)}">${label}</button>`).join('')}
          ${lifecycleButtons.length === 0 ? '<div class="empty">No further transitions.</div>' : ''}
        </div>
        <div class="error-box" id="lifecycle-error" hidden></div>
      </div>

      <div id="tab-config" class="tab-panel">
        <h3>Configuration (draft only)</h3>
        <form id="form-election-config" class="stack-form">
          <label>Name<input type="text" name="name" value="${escapeHtml(e.name)}" maxlength="200" required></label>
          <label>Description<textarea name="description" rows="2" maxlength="2000">${escapeHtml(e.description || '')}</textarea></label>
          <label class="check-label">
            <input type="checkbox" name="showLiveResults" ${e.showLiveResults ? 'checked' : ''}>
            Show live vote totals while the election is open
          </label>
          <div class="form-row">
            <label>Start (ISO UTC)<input type="text" name="startAt" value="${escapeHtml(e.startAt || '')}" placeholder="2026-01-01T08:00:00Z"></label>
            <label>End (ISO UTC)<input type="text" name="endAt" value="${escapeHtml(e.endAt || '')}" placeholder="2026-01-07T20:00:00Z"></label>
          </div>
          <div class="btn-row"><button type="submit" class="btn btn-primary">Save configuration</button></div>
          <div class="error-box" id="cfg-error" hidden></div>
        </form>
      </div>

      <div id="tab-candidates" class="tab-panel">
        <h3>Candidates / ballot options</h3>
        ${e.status === 'draft' ? `
        <form id="form-add-candidate" class="stack-form panel">
          <div class="form-row">
            <label>Name<input type="text" name="name" maxlength="200" required></label>
            <label>Party / affiliation<input type="text" name="party" maxlength="200"></label>
          </div>
          <label>Description<input type="text" name="description" maxlength="2000"></label>
          <div class="btn-row"><button type="submit" class="btn btn-primary">Add option</button></div>
          <div class="error-box" id="cand-error" hidden></div>
        </form>` : '<div class="banner banner-muted">Candidates can only be changed while the election is a draft.</div>'}
        <div id="candidate-list" class="table-wrap"><div class="empty">Loading…</div></div>
      </div>

      <div id="tab-eligibility" class="tab-panel">
        <h3>Voter list</h3>
        <p class="muted small">Only users on this list can cast a ballot.
        Eligibility and ballots are stored separately: the voter list never
        records any choice.</p>
        <form id="form-eligibility" class="inline-form panel">
          <input type="text" name="username" placeholder="username" required>
          <button type="submit" class="btn btn-primary">Add to voter list</button>
        </form>
        <div class="error-box" id="elig-error" hidden></div>
        <div id="eligibility-list" class="table-wrap"><div class="empty">Loading…</div></div>
      </div>

      <div id="tab-admresults" class="tab-panel">
        <h3>Results</h3>
        <div class="btn-row">
          <button class="btn btn-secondary" id="btn-run-tally">Run tally</button>
          <a class="btn btn-secondary" href="#/results/${e.id}">Open results page</a>
        </div>
        <div id="tally-output" class="table-wrap"><div class="empty">No tally has been run in this session.</div></div>
      </div>
    </section>`;

  bindTabs(view);
  function bindTabs(view) {
    view.querySelectorAll('.tab').forEach(tab => {
      tab.addEventListener('click', () => {
        view.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
        view.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
        tab.classList.add('active');
        view.querySelector('#tab-' + tab.getAttribute('data-tab')).classList.add('active');
      });
    });
  }

  // Lifecycle transitions
  view.querySelectorAll('[data-transition]').forEach(btn => {
    btn.addEventListener('click', async () => {
      if (!window.confirm(btn.getAttribute('data-confirm'))) return;
      const errBox = document.getElementById('lifecycle-error');
      errBox.hidden = true;
      try {
        await api('elections/' + e.id + '/status', {
          method: 'POST', body: { status: btn.getAttribute('data-transition') }
        });
        toast('Election updated.');
        renderAdminElection(view);
      } catch (err) {
        errBox.textContent = err.message; errBox.hidden = false;
      }
    });
  });

  // Config
  const cfgForm = document.getElementById('form-election-config');
  cfgForm.addEventListener('submit', async (ev) => {
    ev.preventDefault();
    const fd = new FormData(cfgForm);
    const errBox = document.getElementById('cfg-error');
    errBox.hidden = true;
    try {
      await api('elections/' + e.id, {
        method: 'POST',
        body: {
          name: fd.get('name'),
          description: fd.get('description') || '',
          showLiveResults: fd.get('showLiveResults') === 'on',
          startAt: (fd.get('startAt') || '').trim(),
          endAt: (fd.get('endAt') || '').trim(),
        }
      });
      toast('Configuration saved.');
    } catch (err) {
      errBox.textContent = err.message; errBox.hidden = false;
    }
  });

  // Candidates
  const candList = document.getElementById('candidate-list');
  async function loadCandidates() {
    const data = await api('elections/' + e.id + '/candidates');
    const cands = data.candidates || [];
    candList.innerHTML = cands.length === 0 ? '<div class="empty">No ballot options yet.</div>' :
      `<table><thead><tr><th>#</th><th>Name</th><th>Party</th><th>Description</th>${e.status === 'draft' ? '<th></th>' : ''}</tr></thead><tbody>` +
      cands.map(c => `<tr><td>${c.position}</td><td>${escapeHtml(c.name)}</td><td>${escapeHtml(c.party || '—')}</td><td>${escapeHtml(c.description || '—')}</td>` +
      (e.status === 'draft' ? `<td><button class="btn btn-link btn-danger" data-remove="${c.id}" data-name="${escapeHtml(c.name)}">Remove</button></td>` : '') + '</tr>').join('') +
      '</tbody></table>';
    candList.querySelectorAll('[data-remove]').forEach(btn => {
      btn.addEventListener('click', async () => {
        if (!window.confirm('Remove option ' + btn.getAttribute('data-name') + '?')) return;
        try {
          await api('elections/' + e.id + '/candidates/remove', {
            method: 'POST', body: { candidateId: Number(btn.getAttribute('data-remove')) }
          });
          await loadCandidates();
        } catch (err) { toast(err.message, 'error'); }
      });
    });
  }
  const candForm = document.getElementById('form-add-candidate');
  if (candForm) {
    candForm.addEventListener('submit', async (ev) => {
      ev.preventDefault();
      const fd = new FormData(candForm);
      const errBox = document.getElementById('cand-error');
      errBox.hidden = true;
      try {
        await api('elections/' + e.id + '/candidates', {
          method: 'POST',
          body: { name: fd.get('name'), party: fd.get('party') || '',
                  description: fd.get('description') || '' }
        });
        candForm.reset();
        await loadCandidates();
      } catch (err) {
        errBox.textContent = err.message; errBox.hidden = false;
      }
    });
  }
  await loadCandidates();

  // Eligibility
  const eligList = document.getElementById('eligibility-list');
  async function loadEligibility() {
    const data = await api('elections/' + e.id + '/eligibility');
    const users = data.eligible || [];
    eligList.innerHTML = users.length === 0 ? '<div class="empty">No voters on the list yet.</div>' :
      `<table><thead><tr><th>ID</th><th>Username</th><th>Display name</th><th></th></tr></thead><tbody>` +
      users.map(u => `<tr><td>${u.id}</td><td>${escapeHtml(u.username)}</td><td>${escapeHtml(u.displayName)}</td>
        <td><button class="btn btn-link btn-danger" data-revoke="${u.id}">Remove</button></td></tr>`).join('') +
      '</tbody></table>';
    eligList.querySelectorAll('[data-revoke]').forEach(btn => {
      btn.addEventListener('click', async () => {
        if (!window.confirm('Remove this voter from the list?')) return;
        try {
          await api('elections/' + e.id + '/eligibility', {
            method: 'POST', body: { userId: Number(btn.getAttribute('data-revoke')), action: 'revoke' }
          });
          await loadEligibility();
        } catch (err) { toast(err.message, 'error'); }
      });
    });
  }
  const eligForm = document.getElementById('form-eligibility');
  eligForm.addEventListener('submit', async (ev) => {
    ev.preventDefault();
    const fd = new FormData(eligForm);
    const errBox = document.getElementById('elig-error');
    errBox.hidden = true;
    try {
      await api('elections/' + e.id + '/eligibility', {
        method: 'POST', body: { username: fd.get('username'), action: 'grant' }
      });
      eligForm.reset();
      await loadEligibility();
    } catch (err) {
      errBox.textContent = err.message; errBox.hidden = false;
    }
  });
  await loadEligibility();

  // Tally
  document.getElementById('btn-run-tally').addEventListener('click', async () => {
    const out = document.getElementById('tally-output');
    out.innerHTML = '<div class="empty">Running tally…</div>';
    try {
      const data = await api('elections/' + e.id + '/results');
      out.innerHTML = `
        <table><thead><tr><th>Option</th><th>Votes</th></tr></thead><tbody>
        ${data.results.map(c => `<tr><td>${escapeHtml(c.name)}</td><td>${c.votes}</td></tr>`).join('')}
        </tbody></table>
        <p class="muted small">Ballots: ${data.totalBallots} · Participants: ${data.totalParticipations} ·
        Integrity: ${data.integrityOk ? 'OK' : 'MISMATCH'}</p>`;
    } catch (err) {
      out.innerHTML = `<div class="empty">${escapeHtml(err.message)}</div>`;
    }
  });
}

// ---------------------------------------------------------------------------
// Views: observer
// ---------------------------------------------------------------------------

async function renderObserver(view) {
  if (!state.user || state.user.role !== 'observer') { go('#/elections'); return; }
  view.innerHTML = `
    <section class="card">
      <header class="page-head-inline"><h2>Observer view</h2></header>
      <p class="muted small">Read-only access to your organization's elections
      and audit trail. Observers cannot change any election data.</p>
      <h3>Elections</h3>
      <div id="observer-elections" class="election-list"><div class="empty">Loading…</div></div>
      <h3>Audit trail</h3>
      <div id="observer-audit" class="table-wrap"><div class="empty">Loading…</div></div>
    </section>`;
  try {
    const data = await api('elections');
    const box = document.getElementById('observer-elections');
    const list = data.elections || [];
    box.innerHTML = list.length === 0 ? '<div class="empty">No elections in your organization yet.</div>' :
      list.map(e => `
        <div class="election-row">
          <div class="election-row-main">
            <a href="#/results/${e.id}" class="election-title">${escapeHtml(e.name)}</a>
            <div class="muted small">${escapeHtml(e.description || '')}</div>
          </div>
          <div class="election-row-side">
            <span class="pill pill-${{open:'success',closed:'warn',published:'info',draft:'muted-badge',archived:'muted-badge'}[e.status]}">${fmtStatus(e.status)}</span>
            <a class="btn btn-link" href="#/results/${e.id}">Results</a>
          </div>
        </div>`).join('');
    const audit = await api('audit');
    const aBox = document.getElementById('observer-audit');
    const entries = audit.entries || [];
    aBox.innerHTML = entries.length === 0 ? '<div class="empty">No audit events yet.</div>' :
      `<table><thead><tr><th>Time</th><th>Actor</th><th>Action</th><th>Election</th><th>Detail</th></tr></thead><tbody>` +
      entries.map(ev => `<tr><td class="mono">${escapeHtml(ev.ts)}</td><td>${escapeHtml(ev.actor)}</td><td>${escapeHtml(ev.action)}</td><td>${ev.electionId || '—'}</td><td>${escapeHtml(ev.detail || '')}</td></tr>`).join('') +
      '</tbody></table>';
  } catch (err) {
    toast(err.message, 'error');
  }
}

// ---------------------------------------------------------------------------
// Views: settings
// ---------------------------------------------------------------------------

function renderSettings(view) {
  if (!state.user) { go('#/login'); return; }
  view.innerHTML = `
    <section class="card settings-card">
      <header class="settings-hero">
        <img src="/assets/electravote-logo.png" alt="ElectraVote" class="settings-logo">
        <div><span class="settings-eyebrow">PROFILE & SECURITY</span><h2>Account settings</h2><p>Manage your profile details and keep your account secure.</p></div>
        <a class="btn settings-action settings-back" href="#/elections"><svg viewBox="0 0 20 20" aria-hidden="true"><path d="M15.5 10H4.5M9 5l-5 5 5 5"/></svg><span>Back to elections</span></a>
      </header>
      <section class="settings-section" aria-labelledby="profile-details-title">
        <div class="settings-section-heading"><div><h3 id="profile-details-title">Profile details</h3><p>Your account information and access level.</p></div><span class="profile-initial" aria-hidden="true">${escapeHtml((state.user.displayName || state.user.username || 'V').trim().charAt(0).toUpperCase())}</span></div>
        <div class="profile-details-grid">
          <div><span class="meta-label">Display name</span><strong>${escapeHtml(state.user.displayName || 'Not set')}</strong></div>
          <div><span class="meta-label">Username</span><strong>${escapeHtml(state.user.username)}</strong></div>
          <div><span class="meta-label">Account type</span><strong>${escapeHtml(roleLabel(state.user.role))}</strong></div>
        </div>
      </section>
      <section class="settings-section security-section" aria-labelledby="security-title">
        <div class="settings-section-heading"><div><h3 id="security-title">Password and security</h3><p>Update your password to protect your account.</p></div><span class="security-mark" aria-hidden="true"><svg viewBox="0 0 24 24"><rect x="4" y="10" width="16" height="11" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3m-4 5v2"/></svg></span></div>
        <p class="settings-notice">Changing your password signs you out everywhere.</p>
      <form id="form-password" class="stack-form">
        <label>Current password<input type="password" name="currentPassword" autocomplete="current-password" required></label>
        <label>New password (min 8 chars)<input type="password" name="newPassword" minlength="8" autocomplete="new-password" required></label>
        <div class="error-box" id="pwd-error" hidden></div>
        <button type="submit" class="btn btn-primary settings-action"><svg viewBox="0 0 20 20" aria-hidden="true"><rect x="4" y="8.5" width="12" height="9" rx="2"/><path d="M6.5 8.5V6a3.5 3.5 0 0 1 7 0v2.5M10 12v2"/></svg><span>Update password</span></button>
      </form>
      </section>
    </section>`;
  const form = document.getElementById('form-password');
  form.addEventListener('submit', async (ev) => {
    ev.preventDefault();
    const fd = new FormData(form);
    const errBox = document.getElementById('pwd-error');
    errBox.hidden = true;
    try {
      await api('auth/password', {
        method: 'POST',
        body: { currentPassword: fd.get('currentPassword'), newPassword: fd.get('newPassword') }
      });
      state.user = null;
      toast('Password updated. Please log in again.');
      go('#/login');
    } catch (err) {
      errBox.textContent = err.message; errBox.hidden = false;
    }
  });
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

(async function boot() {
  // Rehydrate the session from the HttpOnly cookie.
  try {
    const r = await api('me');
    state.user = r.user;
  } catch (e) {
    state.user = null;
  }
  navigate();
})();

})();
