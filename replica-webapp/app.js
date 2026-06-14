// ── REPLICA_ dashboard — reads live data from the extension via content-script bridge ──
const CAT_LABELS={bio:'Bio-demographic',geo:'Geographic',prof:'Professional',econ:'Economic',socio:'Socio-cultural',psycho:'Psycho-behavioural'};
const CAT_ORDER=['bio','geo','prof','econ','socio','psycho'];
const CAT_SUM={
  bio:'Age and health signals inferred from browsing.',
  geo:'Location inferred from IP, commute and local searches.',
  prof:'Job role and skills inferred from platforms and tools.',
  econ:'Income and spending profile inferred from activity.',
  socio:'Values and affiliations inferred from networks.',
  psycho:'Emotional traits and habits inferred from behaviour.'
};
let profile=null,sentence=null,activeTab='device',mWk=0,mSelectedDay=isoDay(new Date()),poll=null;
let arduinoState={connected:false,ip:null};

/* ── messaging via content-script bridge ── */
let _seq=0;const _pending={};
window.addEventListener('message',e=>{
  if(e.source!==window)return;const d=e.data;
  if(!d)return;
  if(d.__replica==='res'){
    const cb=_pending[d.reqId];
    if(cb){delete _pending[d.reqId];d.error?cb(null):cb(d.response);}
    return;
  }
  if(d.__replica==='push'){ onExtensionPush(); }
});

// Push from extension (chrome.storage.onChanged): re-fetch and re-render.
// Coalesce bursts so a rapid-fire change storm only triggers one refresh.
let _pushPending=false;
function onExtensionPush(){
  if(_pushPending) return;
  _pushPending=true;
  setTimeout(()=>{
    _pushPending=false;
    msg({type:'GET_PROFILE'},r=>{
      if(!r) return;
      profile=r.profile; sentence=r.sentence;
      if(r.arduino) arduinoState=r.arduino;
      render();
    });
  },50);
}
function msg(m,cb){msgRetry(m,cb,0);}
function msgRetry(m,cb,attempt){
  if(!document.getElementById('replica-ext-id')){
    if(attempt<6){setTimeout(()=>msgRetry(m,cb,attempt+1),150);return;}
    cb&&cb(null);return;
  }
  const reqId=++_seq;let done=false;
  _pending[reqId]=r=>{done=true;
    if(r===null&&attempt<4){setTimeout(()=>msgRetry(m,cb,attempt+1),140*(attempt+1));return;}
    cb&&cb(r);
  };
  window.postMessage({__replica:'req',reqId,payload:m},'*');
  setTimeout(()=>{if(!done&&_pending[reqId]){const f=_pending[reqId];delete _pending[reqId];f(null);}},1500);
}

/* ── connect ── */
function attemptConnect(){
  console.log('[Replica] connecting…');
  msg({type:'GET_PROFILE'},r=>{
    if(r){console.log('[Replica] connected');boot(r);}
    else{console.warn('[Replica] no extension');showNC();}
  });
}
function boot(r){
  profile=r.profile;sentence=r.sentence;
  if(r.arduino) arduinoState=r.arduino;
  hideNC();render();
  if(poll)clearInterval(poll);
  poll=setInterval(()=>msg({type:'GET_PROFILE'},r=>{if(r){profile=r.profile;sentence=r.sentence;if(r.arduino) arduinoState=r.arduino;render();}}),8000);
}
function showNC(){document.getElementById('notConnected').classList.add('show');document.getElementById('app').classList.remove('show');}
function hideNC(){document.getElementById('notConnected').classList.remove('show');document.getElementById('app').classList.add('show');}

/* ── render ── */
function render(){if(!profile||!sentence)return;rMeta();rSentence();rIslands();rLog();rMem();rProf();rLed();}

function rMeta(){
  const pts=CAT_ORDER.reduce((s,c)=>s+(profile.categories[c].dataPoints?profile.categories[c].dataPoints.length:0),0);
  document.getElementById('mPts').textContent=pts;
  const last=profile.lastUpdated?new Date(profile.lastUpdated).toLocaleTimeString('en-US',{hour:'2-digit',minute:'2-digit',hour12:false}):'—';
  document.getElementById('mLast').textContent=last;
}

function rLed(){
  const hasDC=CAT_ORDER.some(c=>profile.categories[c].isDecontextualized);
  document.getElementById('devLed').className='device-led'+(hasDC?' alert':'');
  document.getElementById('pillAlert').style.display=hasDC?'inline-block':'none';
}

function rSentence(){
  const el=document.getElementById('sentenceEl'),v=sentence.vocables;
  // Sentence pattern — must mirror background.js `buildSentence()` and popup.js.
  const pts=[
    {t:'IDENTIFIED AS ',p:1},{c:'bio'},
    {t:', WORKING AS ',p:1},{c:'prof'},
    {t:', LOCATED IN ',p:1},{c:'geo'},
    {t:', NETWORKED WITHIN ',p:1},{c:'socio'},
    {t:', VALUED AS ',p:1},{c:'econ'},
    {t:', AND EXHIBITING ',p:1},{c:'psycho'},
    {t:'.',p:1}
  ];
  el.innerHTML='';
  pts.forEach(pt=>{
    if(pt.p){el.appendChild(document.createTextNode(pt.t));return;}
    const voc=v[pt.c],nil=voc==null;
    const st=profile.categories[pt.c];
    let col='';if(!nil){if(st.isDecontextualized)col='color:var(--accent-decontext)';else if(st.poisonLevel>0)col='color:var(--accent-poison)';else if(st.amplifyLevel>0)col='color:var(--accent-amplify)';}
    el.insertAdjacentHTML('beforeend',`<span style="${nil?'color:#555':col}">[${nil?'?':voc}]</span>`);
  });
}

function titleCase(s){
  if(!s) return s;
  return s.toLowerCase().replace(/(^|[\s.\-/])([a-z])/g,(_,p,c)=>p+c.toUpperCase());
}
function rIslands(){
  const grid=document.getElementById('catGrid');
  grid.innerHTML='';
  CAT_ORDER.forEach(cat=>{
    const st=profile.categories[cat],voc=sentence.vocables[cat],nil=voc==null;
    const c=document.createElement('div');c.className='cat-card';
    if(nil)c.classList.add('nil');else if(st.isDecontextualized)c.classList.add('dc');
    let status='<span class="cat-status">STABLE</span>';
    if(nil)status='<span class="cat-status inactive">AWAITING</span>';
    else if(st.poisonLevel>0) status='<span class="cat-status poi">POISONED</span>';
    else if(st.amplifyLevel>0)status='<span class="cat-status amp">AMPLIFIED</span>';
    else if(st.weight>0&&st.weight<0.3) status='<span class="cat-status inactive">UNSTABLE</span>';
    const n=st.dataPoints?st.dataPoints.length:0;
    const w=(st.weight||0).toFixed(2);
    const prop=st.propagation>=7?'HIGH':st.propagation>=3?'MID':'LOW';
    const age=st.dataPoints&&st.dataPoints.length?timeAgo(st.dataPoints[0].timestamp||profile.lastUpdated):'—';
    const eyebrow=CAT_LABELS[cat].toUpperCase();
    const titleText=nil?'—':titleCase(voc);
    let dcCount=0;
    if(st.isDecontextualized){
      dcCount=(st.dataPoints||[]).filter(dp=>dp.decontextualized).length||1;
    }
    const dcLine=dcCount>0?`<div class="cat-dc-count">${dcCount} EVENT${dcCount!==1?'S':''} DECONTEXTUALIZED</div>`:'';
    c.innerHTML=`
      <div class="cat-head">
        <span class="cat-eyebrow">${eyebrow}</span>
        ${status}
      </div>
      <div class="cat-title">${titleText}</div>
      <div class="cat-sum">${nil?'Awaiting signal. Browse the web to populate this category.':CAT_SUM[cat]}</div>
      <div class="cat-pills">
        <span class="cat-pill">WEIGHT: ${w}</span>
        <span class="cat-pill">AGE: ${age}</span>
        <span class="cat-pill">PROPAGATION:${prop}</span>
      </div>
      <div class="cat-foot">
        <span>${n} EVENT${n!==1?'S':''} RECORDED</span>
        <span>&gt;</span>
      </div>
      ${dcLine}`;
    if(!nil)c.addEventListener('click',()=>openDetail(cat));
    grid.appendChild(c);
  });
}

function rLog(){
  const actions=profile.actions||[];
  const barsEl=document.getElementById('bars'),hrsEl=document.getElementById('hrs'),evEl=document.getElementById('evList');
  const now=new Date();
  const todayStr=now.toDateString();

  // Collect system-detected decontextualizations across all categories
  // (data points flagged with decontextualized:true), turning them into
  // synthetic stream events.
  const dcEvents=[];
  CAT_ORDER.forEach(cat=>{
    const st=profile.categories[cat];
    if(!st||!st.dataPoints)return;
    st.dataPoints.forEach(dp=>{
      if(!dp.decontextualized)return;
      dcEvents.push({
        action:'decontext',
        category:cat,
        timestamp:dp.timestamp,
        url:dp.url,
        title:dp.title,
        signal:dp.signal,
        system:true,
      });
    });
  });

  // Merge user actions + system decontext events, newest first
  const stream=[...actions,...dcEvents].sort((a,b)=>b.timestamp-a.timestamp);

  // 24-hour chart (01..24 like Figma reference)
  const n=24,bkts=Array.from({length:n},()=>({p:0,a:0,d:0}));
  stream.forEach(a=>{
    const d=new Date(a.timestamp);
    if(d.toDateString()!==todayStr)return;
    const i=d.getHours();
    if(i<0||i>=n)return;
    if(a.action==='poison')bkts[i].p++;
    else if(a.action==='amplify')bkts[i].a++;
    else bkts[i].d++;   // both user-action decontext and system decontext
  });
  const mx=Math.max(1,...bkts.map(b=>b.p+b.a+b.d));
  barsEl.innerHTML='';hrsEl.innerHTML='';
  bkts.forEach((b,i)=>{
    const tot=b.p+b.a+b.d;
    const bar=document.createElement('div');
    bar.className='bar';
    if(tot>0){
      bar.classList.add('has-data');
      // Stacked segments (no blending): poi at the base, then amp,
      // then dc on top. `flex: <count>` makes each segment take a
      // share of the bar proportional to its count — robust to
      // browser quirks with % heights inside a column flex.
      [['poi',b.p],['amp',b.a],['dc',b.d]].forEach(([cls,val])=>{
        if(val<=0) return;
        const seg=document.createElement('div');
        seg.className='bar-seg '+cls;
        seg.style.flex=`${val} 0 0`;
        bar.appendChild(seg);
      });
    }
    bar.style.height=`${Math.max(4,(tot/mx)*100)}%`;
    barsEl.appendChild(bar);
    const hc=document.createElement('div');hc.className='hr';hc.textContent=String(i+1).padStart(2,'0');hrsEl.appendChild(hc);
  });

  if(!stream.length){evEl.innerHTML='<div class="ev-empty">No events yet. Browse the web to populate your Data Double.</div>';return;}
  evEl.innerHTML='';
  stream.slice(0,20).forEach(a=>{
    const d=document.createElement('div');d.className='ev';
    const t=new Date(a.timestamp).toLocaleTimeString('en-US',{hour:'2-digit',minute:'2-digit',hour12:false});
    const cat=CAT_LABELS[a.category]||a.category;
    if(a.action==='decontext'){
      const lbl='DECONTEXTUALISED';
      const intLabel=a.system?'SYSTEM':'USER';
      const desc=a.system
        ? `${cat} · "${a.title||a.url||'context shift detected'}"`
        : (a.fromVocable&&a.toVocable?`${cat} · "${a.fromVocable}" → "${a.toVocable}"`:cat);
      d.innerHTML=`<span class="ev-type dc">${lbl}</span><span class="ev-int dc">${intLabel}</span><span class="ev-desc">${desc}</span><span class="ev-time">${t}</span>`;
    } else {
      const isPoison=a.action==='poison';
      const lbl=isPoison?'POISONED':'AMPLIFIED';
      const iL=a.intensity>=3?'HIGH':a.intensity===2?'MID':'LOW';
      const actCls=isPoison?'poi':'amp';
      const ch=a.fromVocable&&a.toVocable?`${cat} · "${a.fromVocable}" → "${a.toVocable}"`:cat;
      d.innerHTML=`<span class="ev-type ${actCls}">${lbl}</span><span class="ev-int ${actCls}">${iL}</span><span class="ev-desc">${ch}</span><span class="ev-time">${t}</span>`;
    }
    evEl.appendChild(d);
  });
}

/* detail (info only — NO actions) — new-style two-column layout */
function openDetail(cat){
  const st=profile.categories[cat],voc=sentence.vocables[cat];
  const prop=st.propagation>=7?'HIGH':st.propagation>=3?'MID':'LOW';
  const age=st.dataPoints&&st.dataPoints.length?timeAgo(st.dataPoints[0].timestamp||profile.lastUpdated):'—';
  const isDC=!!st.isDecontextualized;

  // Match the status chip from the closed card
  let statusHTML='<span class="cat-status">STABLE</span>';
  if(voc==null) statusHTML='<span class="cat-status inactive">AWAITING</span>';
  else if(st.poisonLevel>0) statusHTML='<span class="cat-status poi">POISONED</span>';
  else if(st.amplifyLevel>0) statusHTML='<span class="cat-status amp">AMPLIFIED</span>';
  else if(st.weight>0&&st.weight<0.3) statusHTML='<span class="cat-status inactive">UNSTABLE</span>';

  let dcBox='';
  const alertsEnabled=!(profile.prefs && profile.prefs.decontextAlerts===false);
  if(alertsEnabled&&isDC&&profile.decontextFlags&&profile.decontextFlags[cat]){
    dcBox=`<div class="dc-card">
      <div class="dc-title">DECONTEXTUALIZATION<span class="info-i" onclick="openModal('decontext')">i</span></div>
      <div class="dc-body">${profile.decontextFlags[cat].reason||'Data used outside its original context.'}</div>
    </div>`;
  }

  const dps=(st.dataPoints||[]).slice(0,30);
  const acts=(profile.actions||[]).filter(a=>a.category===cat);
  const signalLabel=s=>s?s.replace(/_/g,' ').toUpperCase():'';
  const domainOf=url=>{try{return new URL(url).hostname.replace(/^www\./,'');}catch{return url;}};
  // Fallback for existing data points without the per-point flag:
  // mark the one URL recorded in decontextFlags as decontextualized.
  const flaggedUrl=isDC&&profile.decontextFlags&&profile.decontextFlags[cat]?profile.decontextFlags[cat].url:null;
  const isPointDC=dp=>!!(dp.decontextualized||(flaggedUrl&&dp.url===flaggedUrl));

  // Merge browse-derived signals + user actions on this category, newest first.
  const rows=[];
  dps.forEach(dp=>{
    const dcRow=isPointDC(dp);
    const sig=dp.signal||dp.topSignal||st.topSignal||'';
    const desc=dp.title?(dp.title.length>120?dp.title.slice(0,120)+'…':dp.title):(sig?signalLabel(sig):domainOf(dp.url||''));
    rows.push({
      ts: dp.timestamp,
      tag: dcRow?'DECONTEXTUALISED':'DATA COLLECTED',
      tagCls: dcRow?'is-dc':'',
      desc: desc||'—'
    });
  });
  acts.forEach(a=>{
    if(a.action==='poison' || a.action==='amplify'){
      const lbl=a.action==='poison'?'POISONED':'AMPLIFIED';
      const cls=a.action==='poison'?'is-poi':'is-amp';
      const iL=a.intensity>=3?'HIGH':a.intensity===2?'MID':'LOW';
      const change=a.fromVocable&&a.toVocable?`"${a.fromVocable}" → "${a.toVocable}"`:`intensity ${iL}`;
      rows.push({ ts: a.timestamp, tag: lbl, tagCls: cls, desc: change });
    } else if(a.action==='decontext'){
      const change=a.fromVocable&&a.toVocable?`"${a.fromVocable}" → "${a.toVocable}"`:'context shift';
      rows.push({ ts: a.timestamp, tag: 'DECONTEXTUALISED', tagCls: 'is-dc', desc: change });
    }
  });
  rows.sort((x,y)=>y.ts-x.ts);
  const visibleRows=rows.slice(0,15);

  const signalsHTML=visibleRows.length?`
    <div class="signal-eyebrow">${rows.length} SIGNAL${rows.length!==1?'S':''} COLLECTED</div>
    ${visibleRows.map(r=>{
      const t=new Date(r.ts).toLocaleTimeString('en-US',{hour:'2-digit',minute:'2-digit',hour12:false});
      return `<div class="signal-row">
        <span class="signal-tag ${r.tagCls}">${r.tag}</span>
        <span class="signal-desc">${r.desc}</span>
        <span class="signal-time">${t}</span>
      </div>`;
    }).join('')}
  `:'<div class="ev-empty">No signals collected yet. Browse the web to populate this category.</div>';

  document.getElementById('detailBody').innerHTML=`
    <div class="detail-grid">
      <div class="detail-left">

        <!-- Category title card -->
        <div class="ds-card">
          <div class="cat-head" style="margin-bottom:var(--space-md)">
            <span class="cat-eyebrow">${CAT_LABELS[cat].toUpperCase()}</span>
            ${statusHTML}
          </div>
          <div class="detail-title">${voc?titleCase(voc):'No data yet'}</div>
          <div class="detail-summary">${CAT_SUM[cat]||''}</div>
        </div>

        ${dcBox}

        <!-- Metrics -->
        <div class="ds-card">
          <div class="cat-eyebrow" style="margin-bottom:var(--space-md)">METRICS</div>
          <div class="metrics">
            <div class="metric-row">
              <span class="metric-key">Weight</span>
              <span class="metric-desc">A value from 0 to 1 showing how strongly this category influences your overall profile. Higher weight means the system relies on it more.</span>
              <span class="metric-val">${(st.weight||0).toFixed(2)} / 1.00</span>
            </div>
            <div class="metric-row">
              <span class="metric-key">Data age</span>
              <span class="metric-desc">How recent the latest data point is. Recent data carries more weight; older data decays over time.</span>
              <span class="metric-val">${age}</span>
            </div>
            <div class="metric-row">
              <span class="metric-key">Propagation</span>
              <span class="metric-desc">An estimate of how widely this data has spread to third-party brokers. Shown as Low, Mid or High.</span>
              <span class="metric-val">${prop}</span>
            </div>
          </div>
        </div>
      </div>

      <!-- Right column: signal stream -->
      <div class="detail-right">
        ${signalsHTML}
      </div>
    </div>`;
  document.querySelectorAll('.screen').forEach(s=>s.classList.remove('active'));
  document.getElementById('scr-detail').classList.add('active');
}
function goBack(){document.getElementById('scr-detail').classList.remove('active');document.getElementById('scr-'+activeTab).classList.add('active');}

function timeAgo(ts){if(!ts)return'—';const s=(Date.now()-ts)/1000;if(s<60)return Math.floor(s)+'s';if(s<3600)return Math.floor(s/60)+'min';if(s<86400)return Math.floor(s/3600)+'h';return Math.floor(s/86400)+'d';}

/* memory — week-only view with calendar picker */
function startOfWeek(d){
  const r=new Date(d);
  // ISO week — Monday as first day
  const dow=(r.getDay()+6)%7;
  r.setDate(r.getDate()-dow);
  r.setHours(0,0,0,0);
  return r;
}
// LOCAL YYYY-MM-DD — never call toISOString (which is UTC) for "date of day".
function isoDay(d){
  const p=n=>String(n).padStart(2,'0');
  return `${d.getFullYear()}-${p(d.getMonth()+1)}-${p(d.getDate())}`;
}
// Parse a YYYY-MM-DD string back to a LOCAL Date at 00:00. `new Date(str)`
// would otherwise treat it as UTC, off-by-one in many timezones.
function parseLocalDay(s){
  if(!s) return null;
  const [y,m,d]=s.split('-').map(Number);
  return new Date(y, m-1, d);
}

function rMem(){
  // Update "current sentence" preview
  const curEl=document.getElementById('memCurrentSent');
  if(curEl) curEl.textContent=sentence?.text||'—';

  const snaps=(profile.snapshots||[]).slice();
  const now=new Date();
  const ws=startOfWeek(now);
  ws.setDate(ws.getDate()+mWk*7);
  const we=new Date(ws); we.setDate(ws.getDate()+6);

  // Range label + nav state
  const rng=document.getElementById('wRange');
  const prev=document.getElementById('wPrev');
  const next=document.getElementById('wNext');
  const f=d=>d.toLocaleDateString('en-US',{day:'numeric',month:'short'});
  const fY=d=>d.toLocaleDateString('en-US',{year:'numeric'});
  rng.textContent=`${f(ws)} – ${f(we)} ${fY(we)}`;
  prev.disabled=false;
  next.disabled=mWk>=0;

  // Calendar 7-day strip
  const days=document.getElementById('wDays');
  days.innerHTML='';
  const todayKey=isoDay(now);
  const selectedKey=mSelectedDay||'';
  const snapByDay={};
  snaps.forEach(s=>{ if(s.date) (snapByDay[s.date]=snapByDay[s.date]||[]).push(s); });

  for(let i=0;i<7;i++){
    const d=new Date(ws); d.setDate(ws.getDate()+i);
    const key=isoDay(d);
    const btn=document.createElement('button');
    btn.className='mem-day';
    if(snapByDay[key]) btn.classList.add('has-snap');
    if(key===todayKey) btn.classList.add('is-today');
    if(key===selectedKey) btn.classList.add('is-selected');
    if(d>now) btn.classList.add('is-future');
    btn.innerHTML=`
      <span class="mem-day-dow">${d.toLocaleDateString('en-US',{weekday:'short'}).slice(0,3)}</span>
      <span class="mem-day-num">${d.getDate()}</span>`;
    btn.addEventListener('click',()=>{
      mSelectedDay = mSelectedDay===key ? null : key;
      rMem();
    });
    days.appendChild(btn);
  }

  // List: filtered by selected day if any, else whole week
  const list=document.getElementById('wList');
  let pool;
  if(mSelectedDay){
    pool=snaps.filter(s=>s.date===mSelectedDay);
  } else {
    // End-of-week comparison: Sunday at 23:59:59.999 (inclusive)
    const weEnd=new Date(we); weEnd.setHours(23,59,59,999);
    pool=snaps.filter(s=>{
      const d=parseLocalDay(s.date);
      return d && d>=ws && d<=weEnd;
    });
  }
  pool.sort((a,b)=>(b.timestamp||(parseLocalDay(b.date)?.getTime()||0))-(a.timestamp||(parseLocalDay(a.date)?.getTime()||0)));

  list.innerHTML='';
  if(!pool.length){
    list.innerHTML=`<div class="mem-empty">No snapshots ${mSelectedDay?'on this day':'this week'} yet.</div>`;
    return;
  }
  pool.forEach(snap=>{
    const ts=snap.timestamp||(snap.savedAt?new Date(snap.savedAt).getTime():null);
    const timeStr=ts?new Date(ts).toLocaleTimeString('en-US',{hour:'2-digit',minute:'2-digit',hour12:false}):'—';
    const tag=snap.manual?'SAVED':'AUTO';
    const delBtn=snap.timestamp?`<button class="wk-del" data-ts="${snap.timestamp}" title="Delete snapshot">×</button>`:'';
    list.insertAdjacentHTML('beforeend',
      `<div class="wk-entry">
        <div style="min-width:96px;flex-shrink:0">
          <div class="wk-day-n">${timeStr}</div>
          <div class="wk-day-m">${tag}</div>
        </div>
        <div class="wk-right">
          <div class="wk-sent">${snap.sentence||'—'}</div>
        </div>
        ${delBtn}
      </div>`);
  });
}

function deleteSnapshot(timestamp){
  // Optimistic UI: remove locally and re-render immediately.
  if(profile&&profile.snapshots){
    profile.snapshots = profile.snapshots.filter(s=>s.timestamp!==timestamp);
    rMem();
  }
  msg({type:'DELETE_SNAPSHOT',timestamp},r=>{
    if(!r||!r.ok){
      console.warn('[Replica] DELETE_SNAPSHOT failed — reload the extension at chrome://extensions',r);
    }
    msg({type:'GET_PROFILE'},rr=>{ if(rr){profile=rr.profile;sentence=rr.sentence;rMem();} });
  });
}

/* profile */
function rProf(){
  const days=profile.startedAt?Math.max(1,Math.ceil((Date.now()-profile.startedAt)/86400000)):1;
  document.getElementById('pSessions').textContent=days+' day'+(days!==1?'s':'');
  const pts=CAT_ORDER.reduce((s,c)=>s+(profile.categories[c].dataPoints?profile.categories[c].dataPoints.length:0),0);
  document.getElementById('pPoints').textContent=pts;
  document.getElementById('pFirst').textContent=profile.startedAt?new Date(profile.startedAt).toLocaleDateString('en-US',{day:'numeric',month:'short',year:'numeric'}):'—';

  // Action counters — sum user actions + system-detected decontextualisations.
  const actions=profile.actions||[];
  let amp=0,poi=0,dc=0;
  actions.forEach(a=>{
    if(a.action==='amplify') amp++;
    else if(a.action==='poison') poi++;
    else if(a.action==='decontext') dc++;
  });
  CAT_ORDER.forEach(c=>{
    const dps=profile.categories[c]&&profile.categories[c].dataPoints;
    if(!dps) return;
    dps.forEach(dp=>{ if(dp.decontextualized) dc++; });
  });
  document.getElementById('pAmpCount').textContent=amp;
  document.getElementById('pPoiCount').textContent=poi;
  document.getElementById('pDcCount').textContent=dc;

  renderPersonalInfo();
  renderPreferences();
}

function renderPreferences(){
  const prefs=(profile&&profile.prefs)||{decontextAlerts:true,dailySnapshot:{enabled:true,hour:18,minute:0}};
  const ds=prefs.dailySnapshot||{enabled:true,hour:18,minute:0};
  const alertsEl=document.getElementById('prefDecontextAlerts');
  const timeEl=document.getElementById('prefSnapshotTime');
  if(alertsEl && document.activeElement!==alertsEl) alertsEl.checked=!!prefs.decontextAlerts;
  const pad=n=>String(n).padStart(2,'0');
  if(timeEl && document.activeElement!==timeEl){
    timeEl.value=`${pad(ds.hour|0)}:${pad(ds.minute|0)}`;
  }
  const memTimeEl=document.getElementById('memSnapTime');
  if(memTimeEl) memTimeEl.textContent=`${pad(ds.hour|0)}:${pad(ds.minute|0)}`;
}

function savePreferences(){
  const alertsEl=document.getElementById('prefDecontextAlerts');
  const timeEl=document.getElementById('prefSnapshotTime');
  const [h,m]=(timeEl.value||'18:00').split(':').map(Number);
  const payload={
    type:'SET_PREFERENCES',
    decontextAlerts:!!alertsEl.checked,
    dailySnapshot:{ enabled:true, hour:h|0, minute:m|0 }
  };
  msg(payload,r=>{ if(r&&r.ok&&profile){ profile.prefs=r.prefs; renderPreferences(); } });
}

function renderPersonalInfo(){
  const userEl=document.getElementById('pUsername');
  const idEl=document.getElementById('pDeviceId');
  if(!userEl||!idEl) return;
  const name=(profile&&profile.username)||'';
  userEl.textContent=name||'Anonymous';
  const idNode=document.getElementById('replica-ext-id');
  idEl.textContent=(idNode&&idNode.dataset.id)||'—';
  const stEl=document.getElementById('pDevStatus');
  const ipEl=document.getElementById('pDevIp');
  if(stEl) stEl.textContent=arduinoState.connected?'Paired':'Not paired';
  if(ipEl) ipEl.textContent=arduinoState.ip||'—';
}

/* modal */
const MODALS={
  decontext:{t:'Decontextualisation',b:'When data collected in one context is reused to classify you in another — e.g. a medical search reused for a professional risk profile. The data is accurate but used outside its original context.'},
  propagation:{t:'Propagation',b:'An estimate of how widely this data has spread to third-party brokers. Shown as Low, Mid or High — the exact number cannot be known, only approximated.'},
  weight:{t:'Algorithmic weight',b:'A value from 0 to 1 showing how strongly this category influences your overall profile. Higher weight means the system relies on it more.'},
  age:{t:'Data age',b:'How recent the latest data point is. Recent data carries more weight; older data decays over time.'}
};
function openModal(k){const m=MODALS[k];if(!m)return;document.getElementById('modalT').textContent=m.t;document.getElementById('modalB').textContent=m.b;document.getElementById('modalOv').classList.add('on');}
function closeModal(){document.getElementById('modalOv').classList.remove('on');}

/* confirm modal for irreversible actions */
let _confirmCb=null;
function askConfirm(title,body,onYes){
  document.getElementById('confirmT').textContent=title;
  document.getElementById('confirmB').textContent=body;
  _confirmCb=onYes;
  document.getElementById('confirmOv').classList.add('on');
}
function closeConfirm(){
  _confirmCb=null;
  document.getElementById('confirmOv').classList.remove('on');
}

/* nav */
function switchTab(t){
  document.querySelectorAll('.screen').forEach(s=>s.classList.remove('active'));
  document.querySelectorAll('.ds-tab').forEach(b=>b.classList.remove('is-active'));
  document.getElementById('scr-'+t).classList.add('active');
  document.getElementById('nv-'+t).classList.add('is-active');
  activeTab=t;
}
document.addEventListener('DOMContentLoaded',()=>{
  document.getElementById('wPrev').addEventListener('click',()=>{mWk--;mSelectedDay=null;rMem();});
  document.getElementById('wNext').addEventListener('click',()=>{if(mWk<0){mWk++;mSelectedDay=null;rMem();}});
  document.getElementById('wToday').addEventListener('click',()=>{mWk=0;mSelectedDay=isoDay(new Date());rMem();});
  document.getElementById('wList').addEventListener('click',e=>{
    const b=e.target.closest('.wk-del');
    if(!b)return;
    e.stopPropagation();
    deleteSnapshot(Number(b.dataset.ts));
  });

  document.getElementById('btnSaveSnap').addEventListener('click',()=>{
    const btn=document.getElementById('btnSaveSnap');
    const status=document.getElementById('memSaveStatus');
    btn.disabled=true;
    btn.textContent='SAVING…';
    status.className='mem-save-status';
    status.textContent='';
    msg({type:'SAVE_SNAPSHOT'},r=>{
      btn.disabled=false;
      btn.textContent='SAVE TO MEMORY';
      if(r&&r.ok){
        status.className='mem-save-status ok';
        status.textContent=`✓ Snapshot saved · ${new Date(r.snapshot.timestamp).toLocaleTimeString('en-US',{hour:'2-digit',minute:'2-digit',hour12:false})}`;
        // refresh profile to surface the new snapshot
        msg({type:'GET_PROFILE'},rr=>{ if(rr){profile=rr.profile;sentence=rr.sentence;rMem();} });
      } else {
        status.className='mem-save-status';
        status.textContent='Could not save. Try again.';
      }
      setTimeout(()=>{ if(status.textContent.startsWith('✓')) status.textContent=''; },4000);
    });
  });

  ['prefDecontextAlerts','prefSnapshotTime'].forEach(id=>{
    const el=document.getElementById(id);
    if(el) el.addEventListener('change',savePreferences);
  });

  document.getElementById('btnReset').addEventListener('click',()=>{
    askConfirm(
      'Reset all data?',
      'This wipes your entire Data Double — categories, signals, snapshots and decontextualisation logs. The action cannot be undone.',
      ()=>msg({type:'RESET'},()=>setTimeout(attemptConnect,200))
    );
  });
  document.getElementById('btnResetSnaps').addEventListener('click',()=>{
    askConfirm(
      'Reset memory quotes?',
      'All saved sentence snapshots will be removed from your memory. The action cannot be undone.',
      ()=>msg({type:'RESET_SNAPSHOTS'},r=>{
        if(r&&r.ok){
          msg({type:'GET_PROFILE'},rr=>{ if(rr){profile=rr.profile;sentence=rr.sentence;rMem();rProf();} });
        }
      })
    );
  });
  document.getElementById('btnResetDecontext').addEventListener('click',()=>{
    askConfirm(
      'Delete all decontextualisation logs?',
      'Every decontextualisation flag, marked data point and historical alert will be removed. The action cannot be undone.',
      ()=>msg({type:'RESET_DECONTEXT'},r=>{
        if(r&&r.ok){
          msg({type:'GET_PROFILE'},rr=>{ if(rr){profile=rr.profile;sentence=rr.sentence;render();} });
        }
      })
    );
  });
  document.getElementById('confirmYes').addEventListener('click',()=>{
    const cb=_confirmCb;
    closeConfirm();
    if(cb) cb();
  });
});
document.addEventListener('DOMContentLoaded',()=>setTimeout(attemptConnect,0));
