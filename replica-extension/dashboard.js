// ─────────────────────────────────────────────
// REPLICA_ - dashboard.js (new-style)
// ─────────────────────────────────────────────

const CAT_LABELS = {
  bio:    'BIO-DEMOGRAPHIC',
  geo:    'GEOGRAPHIC',
  prof:   'PROFESSIONAL',
  econ:   'ECONOMIC',
  socio:  'SOCIO-CULTURAL',
  psycho: 'PSYCHO-BEHAVIOURAL'
};

const CAT_SUM = {
  bio:    'Age and health signals inferred from browsing.',
  geo:    'Location inferred from IP, commute and local searches.',
  prof:   'Job role and skills inferred from platforms and tools.',
  econ:   'Income and spending profile inferred from activity.',
  socio:  'Values and affiliations inferred from networks.',
  psycho: 'Emotional traits and habits inferred from behaviour.'
};

const CAT_ORDER = ['bio', 'geo', 'prof', 'econ', 'socio', 'psycho'];

let profile = null;
let sentence = null;

document.addEventListener('DOMContentLoaded', () => {
  loadData();
  setInterval(loadData, 15000);
  document.getElementById('headerDate').textContent = new Date().toLocaleDateString('en-US', {
    weekday: 'short', year: 'numeric', month: 'short', day: 'numeric'
  }).toUpperCase();
});

function loadData() {
  chrome.runtime.sendMessage({ type: 'GET_PROFILE' }, res => {
    if (!res) return;
    profile = res.profile;
    sentence = res.sentence;
    render();
  });
}

function render() {
  renderSentence();
  renderDecontext();
  renderCategories();
  renderEventLog();
  renderMemory();
}

function renderSentence() {
  const el = document.getElementById('sentenceBig');
  if (!sentence) return;

  const parts = [
    { text: 'IDENTIFIED AS ', plain: true }, { cat: 'bio' },
    { text: ', LOCATED IN ', plain: true }, { cat: 'geo' },
    { text: ', WORKING AS ', plain: true }, { cat: 'prof' },
    { text: ', VALUED AS ', plain: true }, { cat: 'econ' },
    { text: ', NETWORKED WITHIN ', plain: true }, { cat: 'socio' },
    { text: ', AND EXHIBITING ', plain: true }, { cat: 'psycho' },
    { text: '.', plain: true }
  ];

  el.innerHTML = '';
  parts.forEach(part => {
    if (part.plain) {
      el.appendChild(document.createTextNode(part.text));
    } else {
      const span = document.createElement('span');
      span.className = 'v';
      const v = sentence.vocables[part.cat];
      const isUnresolved = v === null || v === undefined;
      span.textContent = isUnresolved ? '[?]' : v;
      if (isUnresolved) {
        span.classList.add('unresolved');
      } else {
        const state = profile.categories[part.cat];
        if (state.isDecontextualized) span.classList.add('decontext');
        else if (state.poisonLevel > 0) span.classList.add('poisoned');
        else if (state.amplifyLevel > 0) span.classList.add('amplified');
      }
      el.appendChild(span);
    }
  });
}

function renderDecontext() {
  const flags = profile.decontextFlags || {};
  const keys = Object.keys(flags);
  const panel = document.getElementById('decontextPanel');
  const list = document.getElementById('decontextList');

  if (keys.length === 0) {
    panel.classList.remove('visible');
    return;
  }

  panel.classList.add('visible');
  list.innerHTML = '';

  keys.forEach(cat => {
    const flag = flags[cat];
    const entry = document.createElement('div');
    entry.className = 'decontext-entry';
    entry.innerHTML = `
      <div class="decontext-cat">${CAT_LABELS[cat] || cat}</div>
      <div>${flag.reason || 'Cross-category anomaly detected.'}</div>
    `;
    list.appendChild(entry);
  });
}

function renderCategories() {
  const grid = document.getElementById('catGrid');
  grid.innerHTML = '';

  CAT_ORDER.forEach(cat => {
    const state = profile.categories[cat];
    const vocable = sentence.vocables[cat];
    const isUnresolved = vocable === null || vocable === undefined;
    const card = document.createElement('div');
    card.className = 'cat-card';

    let statusHTML = '<span class="cat-status">STABLE</span>';
    if (isUnresolved) {
      card.classList.add('unresolved');
      statusHTML = '<span class="cat-status inactive">AWAITING</span>';
    } else if (state.poisonLevel > 0) {
      statusHTML = '<span class="cat-status poisoned">POISONED</span>';
    } else if (state.amplifyLevel > 0) {
      statusHTML = '<span class="cat-status amplified">AMPLIFIED</span>';
    } else if (state.weight < 0.3) {
      statusHTML = '<span class="cat-status inactive">UNSTABLE</span>';
    }

    const w = (state.weight || 0).toFixed(2);
    const age = state.dataPoints && state.dataPoints.length
      ? timeAgo(state.dataPoints[0].timestamp || profile.lastUpdated)
      : '—';
    const prop = state.propagation >= 7 ? 'HIGH' : state.propagation >= 3 ? 'MID' : 'LOW';
    const n = state.dataPoints ? state.dataPoints.length : 0;
    let dcCount = 0;
    if (state.isDecontextualized) {
      dcCount = (state.dataPoints || []).filter(dp => dp.decontextualized).length || 1;
    }
    const dcLine = dcCount > 0
      ? `<div class="cat-dc-count">${dcCount} EVENT${dcCount !== 1 ? 'S' : ''} DECONTEXTUALIZED</div>`
      : '';

    card.innerHTML = `
      <div class="cat-card-header">
        <div class="cat-name">${CAT_LABELS[cat]}</div>
        ${statusHTML}
      </div>
      <div class="cat-vocable">${isUnresolved ? '—' : titleCase(vocable)}</div>
      <div class="cat-sum">${isUnresolved ? 'Awaiting signal. Browse the web to populate this category.' : CAT_SUM[cat]}</div>
      <div class="cat-pills">
        <span class="cat-pill">WEIGHT: ${w}</span>
        <span class="cat-pill">AGE: ${age}</span>
        <span class="cat-pill">PROPAGATION:${prop}</span>
      </div>
      <div class="cat-foot">
        <span>${n} EVENT${n !== 1 ? 'S' : ''} RECORDED</span>
        <span>&gt;</span>
      </div>
      ${dcLine}
    `;
    grid.appendChild(card);
  });
}

function renderEventLog() {
  const actions = profile.actions || [];
  const chart = document.getElementById('eventChart');
  const hoursRow = document.getElementById('eventHours');
  const list = document.getElementById('eventList');

  // Synthesize stream events from system-detected decontextualizations.
  const dcEvents = [];
  CAT_ORDER.forEach(cat => {
    const st = profile.categories[cat];
    if (!st || !st.dataPoints) return;
    st.dataPoints.forEach(dp => {
      if (!dp.decontextualized) return;
      dcEvents.push({
        action:    'decontext',
        category:  cat,
        timestamp: dp.timestamp,
        url:       dp.url,
        title:     dp.title,
        signal:    dp.signal,
        system:    true,
      });
    });
  });

  const stream = [...actions, ...dcEvents].sort((a, b) => b.timestamp - a.timestamp);

  const n = 24;
  const buckets = Array.from({ length: n }, () => ({ p: 0, a: 0, d: 0 }));
  const today = new Date().toDateString();
  stream.forEach(a => {
    const d = new Date(a.timestamp);
    if (d.toDateString() !== today) return;
    const i = d.getHours();
    if (i < 0 || i >= n) return;
    if (a.action === 'poison')        buckets[i].p++;
    else if (a.action === 'amplify')  buckets[i].a++;
    else                              buckets[i].d++;
  });
  const maxVal = Math.max(1, ...buckets.map(b => b.p + b.a + b.d));

  chart.innerHTML = '';
  hoursRow.innerHTML = '';
  buckets.forEach((b, i) => {
    const total = b.p + b.a + b.d;
    const bar = document.createElement('div');
    bar.className = 'event-bar';
    if (total > 0) {
      if (b.d > 0)                    bar.classList.add('decontext');
      else if (b.p > 0 && b.a > 0)    bar.classList.add('mixed');
      else if (b.p > 0)               bar.classList.add('poison');
      else                            bar.classList.add('amplify');
    }
    bar.style.height = `${Math.max(4, (total / maxVal) * 100)}%`;
    chart.appendChild(bar);

    const h = document.createElement('div');
    h.className = 'event-hour';
    h.textContent = String(i + 1).padStart(2, '0');
    hoursRow.appendChild(h);
  });

  if (stream.length === 0) {
    list.innerHTML = '<div class="no-events">No actions performed yet. Browse normally, then interact via the popup.</div>';
    return;
  }

  list.innerHTML = '';
  stream.slice(0, 20).forEach(a => {
    const entry = document.createElement('div');
    entry.className = 'event-entry';
    const time = new Date(a.timestamp).toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit', hour12: false });
    const cat = CAT_LABELS[a.category] || a.category;
    if (a.action === 'decontext') {
      const tag = a.system ? '<span class="event-int dc">SYSTEM</span>' : '<span></span>';
      const desc = a.system
        ? `${cat} · "${a.title || a.url || 'context shift detected'}"`
        : (a.fromVocable && a.toVocable ? `${cat} · "${a.fromVocable}" → "${a.toVocable}"` : cat);
      entry.innerHTML = `
        <span class="event-action dc">DECONTEXTUALISED</span>
        ${tag}
        <span class="event-desc">${desc}</span>
        <span class="event-time">${time}</span>
      `;
    } else {
      const lbl = a.action === 'poison' ? 'POISONED' : 'AMPLIFIED';
      const iL = a.intensity >= 3 ? 'HIGH' : a.intensity === 2 ? 'MID' : 'LOW';
      const iC = a.intensity >= 3 ? 'high' : a.intensity === 2 ? 'mid' : 'low';
      const ch = a.fromVocable && a.toVocable ? `${cat} · "${a.fromVocable}" → "${a.toVocable}"` : cat;
      entry.innerHTML = `
        <span class="event-action">${lbl}</span>
        <span class="event-int ${iC}">${iL}</span>
        <span class="event-desc">${ch}</span>
        <span class="event-time">${time}</span>
      `;
    }
    list.appendChild(entry);
  });
}

function renderMemory() {
  const snaps = profile.snapshots || [];
  const list = document.getElementById('memoryList');

  if (snaps.length === 0) {
    list.innerHTML = '<div class="no-memory">No snapshots yet. A daily snapshot is saved automatically at 18:00.</div>';
    return;
  }

  list.innerHTML = '';
  snaps.forEach(snap => {
    const entry = document.createElement('div');
    entry.className = 'memory-entry';
    const dateStr = snap.date
      ? new Date(snap.date).toLocaleDateString('en-US', { day: '2-digit', month: 'short', year: 'numeric' }).toUpperCase()
      : '—';
    entry.innerHTML = `
      <div class="memory-date">${dateStr}</div>
      <div class="memory-sentence">${snap.sentence || '—'}</div>
    `;
    list.appendChild(entry);
  });
}

function timeAgo(ts) {
  if (!ts) return '—';
  const s = (Date.now() - ts) / 1000;
  if (s < 60) return Math.floor(s) + 's';
  if (s < 3600) return Math.floor(s / 60) + 'min';
  if (s < 86400) return Math.floor(s / 3600) + 'h';
  return Math.floor(s / 86400) + 'd';
}

function titleCase(s) {
  if (!s) return s;
  return s.toLowerCase().replace(/(^|[\s.\-/])([a-z])/g, (_, p, c) => p + c.toUpperCase());
}
