/* ==========================================================================
   Online Voting System — Frontend Application
   Vanilla JS, no dependencies. Talks to the C++ HTTP server's JSON API.
   ========================================================================== */

(function () {
'use strict';

// ---------------------------------------------------------------------------
// Config & state
// ---------------------------------------------------------------------------

// The vote payload encryption key MUST match the server's VOTE_KEY.
const VOTE_KEY = 'V0t!ngSys_2024_D3moKey#xyz';

const state = {
  voter: null,    // { id, name, hasVoted, password }
  admin: null,    // { username }
  view: 'home',
  selectedCandidate: null,
};

// ---------------------------------------------------------------------------
// API client
// ---------------------------------------------------------------------------

async function api(path, opts) {
  opts = opts || {};
  const init = {
    method: opts.method || 'GET',
    headers: { 'Content-Type': 'application/json' },
  };
  if (opts.body) init.body = typeof opts.body === 'string' ? opts.body : JSON.stringify(opts.body);
  const res = await fetch('/api/' + path, init);
  let data = null;
  try { data = await res.json(); } catch (e) {}
  if (!res.ok) {
    const msg = (data && data.error) || ('HTTP ' + res.status);
    const err = new Error(msg);
    err.status = res.status; err.data = data;
    throw err;
  }
  return data;
}

// ---------------------------------------------------------------------------
// Crypto helpers (mirror of server-side, for ballot encryption)
// ---------------------------------------------------------------------------

// XOR-stream cipher (UTF-8 safe): we operate on bytes via Uint8Array.
function xorCryptBytes(bytes, keyStr) {
  const keyBytes = new TextEncoder().encode(keyStr);
  const out = new Uint8Array(bytes.length);
  for (let i = 0; i < bytes.length; i++) {
    out[i] = bytes[i] ^ keyBytes[i % keyBytes.length];
  }
  return out;
}

function base64EncodeBytes(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
  return btoa(s);
}

function encryptBallot(candidateId, voterId) {
  const plain = candidateId + '|' + voterId;
  const bytes = new TextEncoder().encode(plain);
  const enc = xorCryptBytes(bytes, VOTE_KEY);
  return base64EncodeBytes(enc);
}

// ---------------------------------------------------------------------------
// Router
// ---------------------------------------------------------------------------

const app = document.getElementById('app');

function go(view) {
  state.view = view;
  state.selectedCandidate = null;
  render();
  window.scrollTo({ top: 0, behavior: 'smooth' });
}

function render() {
  const tplId = 'tpl-' + state.view;
  let tpl = document.getElementById(tplId);
  if (!tpl) tpl = document.getElementById('tpl-home');
  app.innerHTML = '';
  app.appendChild(document.importNode(tpl.content, true));
  bindView();
}

// ---------------------------------------------------------------------------
// View bindings
// ---------------------------------------------------------------------------

function bindView() {
  // Generic data-go navigation
  app.querySelectorAll('[data-go]').forEach(el => {
    el.addEventListener('click', () => go(el.getAttribute('data-go')));
  });

  switch (state.view) {
    case 'voter-register':     bindRegister(); break;
    case 'voter-login':        bindVoterLogin(); break;
    case 'voter-dashboard':    bindVoterDashboard(); break;
    case 'admin-login':        bindAdminLogin(); break;
    case 'admin-dashboard':    bindAdminDashboard(); break;
    case 'results':            bindResultsPage(); break;
  }
}

function setBox(id, type, msg) {
  const el = document.getElementById(id);
  if (!el) return;
  el.className = 'result-box ' + type;
  el.textContent = msg;
}

// ----- Register -----
function bindRegister() {
  const form = document.getElementById('form-register');
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const fd = new FormData(form);
    try {
      const r = await api('register', {
        method: 'POST',
        body: {
          name: fd.get('name'),
          nationalId: fd.get('nationalId') || '',
          password: fd.get('password'),
        }
      });
      setBox('reg-result', 'success',
        'Registration successful! Your Voter ID is: ' + r.voterId +
        ' — please save it. You can now log in.');
      // Pre-fill login hint
      const btn = document.createElement('button');
      btn.className = 'btn btn-link';
      btn.textContent = 'Go to Login →';
      btn.onclick = () => go('voter-login');
      document.getElementById('reg-result').appendChild(document.createElement('br'));
      document.getElementById('reg-result').appendChild(btn);
    } catch (err) {
      setBox('reg-result', 'error', err.message);
    }
  });
}

// ----- Voter login -----
function bindVoterLogin() {
  const form = document.getElementById('form-voter-login');
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const fd = new FormData(form);
    try {
      const r = await api('voter/login', {
        method: 'POST',
        body: { voterId: fd.get('voterId'), password: fd.get('password') }
      });
      state.voter = {
        id: r.voterId,
        name: r.name,
        hasVoted: r.hasVoted,
        password: fd.get('password'),
      };
      go('voter-dashboard');
    } catch (err) {
      setBox('voter-login-result', 'error', err.message);
    }
  });
}

// ----- Voter dashboard -----
async function bindVoterDashboard() {
  const badge = document.getElementById('voter-badge');
  badge.textContent = state.voter.name + ' (' + state.voter.id + ')';

  const logout = app.querySelector('[data-action="logout"]');
  logout.addEventListener('click', () => { state.voter = null; go('home'); });

  try {
    const data = await api('candidates');
    const banner = document.getElementById('election-status-box');
    if (!data.electionOpen) {
      banner.innerHTML = '<div class="election-banner closed">⚠️ Election is currently closed. You cannot vote at this time.</div>';
    } else if (state.voter.hasVoted) {
      banner.innerHTML = '<div class="election-banner info">✓ You have already cast your vote. Thank you for participating!</div>';
    } else {
      banner.innerHTML = '<div class="election-banner open">✓ Election is open. Tap a candidate to vote.</div>';
    }
    const grid = document.getElementById('candidate-grid');
    if (!data.candidates || data.candidates.length === 0) {
      grid.innerHTML = '<div class="empty">No candidates have been added yet. Please check back later.</div>';
      return;
    }
    grid.innerHTML = '';
    data.candidates.forEach(c => {
      const card = document.createElement('div');
      card.className = 'candidate-card';
      card.innerHTML = `
        <div class="symbol">${escapeHtml(c.symbol || '🗳️')}</div>
        <div class="name">${escapeHtml(c.name)}</div>
        <div class="party">${escapeHtml(c.party)}</div>
        <div class="position">${escapeHtml(c.position || '')}</div>
      `;
      card.addEventListener('click', () => {
        if (!data.electionOpen) {
          setBox('vote-result', 'error', 'Election is closed. Voting is disabled.');
          return;
        }
        if (state.voter.hasVoted) {
          setBox('vote-result', 'error', 'You have already voted.');
          return;
        }
        confirmVote(c, card);
      });
      grid.appendChild(card);
    });
  } catch (err) {
    setBox('vote-result', 'error', 'Failed to load candidates: ' + err.message);
  }
}

function confirmVote(candidate, cardEl) {
  if (state.selectedCandidate === candidate.id) {
    castVote(candidate);
    return;
  }
  state.selectedCandidate = candidate.id;
  app.querySelectorAll('.candidate-card').forEach(el => el.classList.remove('selected'));
  cardEl.classList.add('selected');
  setBox('vote-result', 'info',
    'You selected: ' + candidate.name + ' (' + candidate.party + '). ' +
    'Tap again to confirm and cast your vote. This action cannot be undone.');
}

async function castVote(candidate) {
  setBox('vote-result', 'info', 'Encrypting your ballot and submitting...');
  const ballot = encryptBallot(candidate.id, state.voter.id);
  try {
    const r = await api('vote', {
      method: 'POST',
      body: {
        voterId: state.voter.id,
        password: state.voter.password,
        ballot: ballot,
      }
    });
    state.voter.hasVoted = true;
    setBox('vote-result', 'success',
      '✓ Vote recorded! Receipt code: ' + r.receipt + ' (timestamp: ' + r.timestamp + ')');
    app.querySelectorAll('.candidate-card').forEach(el => el.classList.remove('selected'));
    state.selectedCandidate = null;
    // Update banner
    const banner = document.getElementById('election-status-box');
    banner.innerHTML = '<div class="election-banner info">✓ You have already cast your vote. Thank you for participating!</div>';
  } catch (err) {
    setBox('vote-result', 'error', err.message);
  }
}

// ----- Admin login -----
function bindAdminLogin() {
  const form = document.getElementById('form-admin-login');
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const fd = new FormData(form);
    try {
      const r = await api('admin/login', {
        method: 'POST',
        body: { username: fd.get('username'), password: fd.get('password') }
      });
      state.admin = { username: r.username };
      go('admin-dashboard');
    } catch (err) {
      setBox('admin-login-result', 'error', err.message);
    }
  });
}

// ----- Admin dashboard -----
function bindAdminDashboard() {
  const badge = document.getElementById('admin-badge');
  badge.textContent = 'Admin: ' + state.admin.username;

  app.querySelector('[data-action="logout"]').addEventListener('click', () => {
    state.admin = null; go('home');
  });

  // Tab switching
  app.querySelectorAll('.tab').forEach(tab => {
    tab.addEventListener('click', () => {
      app.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
      app.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
      tab.classList.add('active');
      document.getElementById('tab-' + tab.getAttribute('data-tab')).classList.add('active');
    });
  });

  bindAdminCandidates();
  bindAdminVoters();
  loadAdminResults();
  loadElectionState();
  loadAuditLog();
  bindAdminPassword();
}

async function bindAdminCandidates() {
  const form = document.getElementById('form-add-candidate');
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const fd = new FormData(form);
    try {
      await api('admin/candidates', {
        method: 'POST',
        body: {
          name: fd.get('name'),
          party: fd.get('party'),
          symbol: fd.get('symbol') || '🗳️',
          position: fd.get('position') || 'President',
        }
      });
      form.reset();
      await loadCandidateList();
    } catch (err) {
      alert('Error: ' + err.message);
    }
  });
  await loadCandidateList();
}

async function loadCandidateList() {
  const wrap = document.getElementById('candidate-list');
  try {
    const data = await api('candidates');
    if (!data.candidates || data.candidates.length === 0) {
      wrap.innerHTML = '<div class="empty">No candidates yet.</div>';
      return;
    }
    let html = '<table><thead><tr><th>ID</th><th>Name</th><th>Party</th><th>Symbol</th><th>Position</th><th>Votes</th><th></th></tr></thead><tbody>';
    data.candidates.forEach(c => {
      html += `<tr>
        <td>${escapeHtml(c.id)}</td>
        <td>${escapeHtml(c.name)}</td>
        <td>${escapeHtml(c.party)}</td>
        <td>${escapeHtml(c.symbol || '')}</td>
        <td>${escapeHtml(c.position || '')}</td>
        <td>${c.votes}</td>
        <td><button class="btn btn-link btn-danger" data-remove="${c.id}">Remove</button></td>
      </tr>`;
    });
    html += '</tbody></table>';
    wrap.innerHTML = html;
    wrap.querySelectorAll('[data-remove]').forEach(btn => {
      btn.addEventListener('click', async () => {
        if (!confirm('Remove candidate ' + btn.getAttribute('data-remove') + '?')) return;
        try {
          await api('admin/candidates/remove', {
            method: 'POST',
            body: { candidateId: btn.getAttribute('data-remove') }
          });
          await loadCandidateList();
        } catch (err) { alert(err.message); }
      });
    });
  } catch (err) {
    wrap.innerHTML = '<div class="empty">Failed to load: ' + escapeHtml(err.message) + '</div>';
  }
}

async function bindAdminVoters() {
  const form = document.getElementById('form-add-voter');
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const fd = new FormData(form);
    try {
      const r = await api('admin/voters', {
        method: 'POST',
        body: {
          name: fd.get('name'),
          nationalId: fd.get('nationalId') || '',
          password: fd.get('password'),
        }
      });
      form.reset();
      alert('Voter added. Voter ID: ' + r.voterId);
      await loadVoterList();
    } catch (err) {
      alert(err.message);
    }
  });
  await loadVoterList();
}

async function loadVoterList() {
  const wrap = document.getElementById('voter-list');
  try {
    const data = await api('admin/voters');
    if (!data.voters || data.voters.length === 0) {
      wrap.innerHTML = '<div class="empty">No voters registered.</div>';
      return;
    }
    let html = '<table><thead><tr><th>ID</th><th>Name</th><th>National ID</th><th>Voted?</th></tr></thead><tbody>';
    data.voters.forEach(v => {
      html += `<tr>
        <td>${escapeHtml(v.id)}</td>
        <td>${escapeHtml(v.name)}</td>
        <td>${escapeHtml(v.nationalId || '—')}</td>
        <td>${v.hasVoted ? '✓ Yes' : '— No'}</td>
      </tr>`;
    });
    html += '</tbody></table>';
    wrap.innerHTML = html;
  } catch (err) {
    wrap.innerHTML = '<div class="empty">Failed to load: ' + escapeHtml(err.message) + '</div>';
  }
}

async function loadAdminResults() {
  const panel = document.getElementById('results-panel');
  if (!panel) return;
  try {
    const data = await api('results');
    renderResults(panel, data);
  } catch (err) {
    panel.innerHTML = '<div class="empty">' + escapeHtml(err.message) + '</div>';
  }
}

function renderResults(container, data) {
  if (!data.results || data.results.length === 0) {
    container.innerHTML = '<div class="empty">No votes have been cast yet.</div>';
    return;
  }
  const leader = data.leader ? '<h3>Current leader: ' + escapeHtml(data.leader) + '</h3>' : '';
  let html = leader + '<p class="muted">Total votes cast: <strong>' + data.totalVotes + '</strong></p>';
  html += '<div class="table-wrap"><table><thead><tr><th>#</th><th>Candidate</th><th>Party</th><th>Votes</th><th>Share</th></tr></thead><tbody>';
  data.results.forEach((c, i) => {
    const pct = c.percent.toFixed(1);
    html += `<tr>
      <td>${i + 1}</td>
      <td>${escapeHtml(c.symbol || '🗳️')} ${escapeHtml(c.name)}</td>
      <td>${escapeHtml(c.party)}</td>
      <td>${c.votes}</td>
      <td>
        ${pct}%
        <div class="vote-bar"><span style="width:${pct}%"></span></div>
      </td>
    </tr>`;
  });
  html += '</tbody></table></div>';
  container.innerHTML = html;
}

async function loadElectionState() {
  const box = document.getElementById('election-state-box');
  if (!box) return;
  try {
    const data = await api('election/status');
    box.innerHTML = '<div class="election-banner ' + (data.electionOpen ? 'open' : 'closed') + '">' +
      (data.electionOpen ? '● Election is OPEN — voting is allowed.' : '● Election is CLOSED — voting is disabled.') +
      '</div>';
  } catch (err) {
    box.innerHTML = '<div class="empty">' + escapeHtml(err.message) + '</div>';
  }
}

async function loadAuditLog() {
  const wrap = document.getElementById('audit-list');
  if (!wrap) return;
  try {
    const data = await api('admin/audit');
    if (!data.entries || data.entries.length === 0) {
      wrap.innerHTML = '<div class="empty">No audit entries yet.</div>';
      return;
    }
    let html = data.entries.map(e =>
      `<div class="audit-row">
        <span class="ts">${escapeHtml(e.timestamp)}</span>
        <span class="actor">${escapeHtml(e.actor)}</span>
        <span class="action">${escapeHtml(e.action)}</span>
        <span class="detail">${escapeHtml(e.detail)}</span>
      </div>`
    ).join('');
    wrap.innerHTML = html;
  } catch (err) {
    wrap.innerHTML = '<div class="empty">' + escapeHtml(err.message) + '</div>';
  }
}

function bindAdminPassword() {
  const form = document.getElementById('form-change-pwd');
  if (!form) return;
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const fd = new FormData(form);
    try {
      await api('admin/password', {
        method: 'POST',
        body: {
          username: state.admin.username,
          oldPassword: fd.get('oldPassword'),
          newPassword: fd.get('newPassword'),
        }
      });
      form.reset();
      setBox('pwd-result', 'success', 'Password updated successfully.');
    } catch (err) {
      setBox('pwd-result', 'error', err.message);
    }
  });
}

// Wire up admin action buttons (refresh, open/close election)
document.addEventListener('click', async (e) => {
  const t = e.target;
  if (!t.matches('[data-action]')) return;
  const action = t.getAttribute('data-action');
  if (action === 'refresh-results') {
    const panel = document.getElementById('results-panel') || document.getElementById('results-page');
    if (panel) {
      panel.innerHTML = '<div class="empty">Loading...</div>';
      try {
        const data = await api('results');
        renderResults(panel, data);
      } catch (err) { panel.innerHTML = '<div class="empty">' + escapeHtml(err.message) + '</div>'; }
    }
  } else if (action === 'refresh-audit') {
    await loadAuditLog();
  } else if (action === 'open-election') {
    try {
      await api('election/status', { method: 'POST', body: { open: true } });
      await loadElectionState();
    } catch (err) { alert(err.message); }
  } else if (action === 'close-election') {
    if (!confirm('Close the election? No further votes will be accepted.')) return;
    try {
      await api('election/status', { method: 'POST', body: { open: false } });
      await loadElectionState();
    } catch (err) { alert(err.message); }
  }
});

// ----- Results page (public) -----
async function bindResultsPage() {
  const panel = document.getElementById('results-page');
  panel.innerHTML = '<div class="empty">Loading...</div>';
  try {
    const data = await api('results');
    renderResults(panel, data);
  } catch (err) {
    panel.innerHTML = '<div class="empty">' + escapeHtml(err.message) + '</div>';
  }
}

// ---------------------------------------------------------------------------
// Utils
// ---------------------------------------------------------------------------

function escapeHtml(s) {
  if (s === null || s === undefined) return '';
  return String(s)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

render();

})();
