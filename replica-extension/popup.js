const CAT_LABELS = { bio:'Bio-demographic', geo:'Geographic', prof:'Professional', econ:'Economic', socio:'Socio-cultural', psycho:'Psycho-behavioural' };
const CAT_ORDER  = ['bio','geo','prof','econ','socio','psycho'];
let profile=null, sentence=null, arduinoState={ connected:false, ip:null };

// ── Init ─────────────────────────────────────────────
document.addEventListener('DOMContentLoaded',()=>{
  load();
  document.getElementById('btnDash').addEventListener('click',()=>chrome.tabs.create({url:'https://alessialarocca.github.io/replica/replica-webapp/index.html'}));
  document.getElementById('btnReset').addEventListener('click',()=>{
    if(!confirm('Reset profile?'))return;
    chrome.runtime.sendMessage({type:'RESET'},()=>load());
  });
  initArduinoPanel();
  // Refresh ogni 2s per aggiornamenti live (es. switch Arduino)
  setInterval(load, 2000);
});

function load(){
  chrome.runtime.sendMessage({type:'GET_PROFILE'},r=>{
    if(!r)return;
    profile=r.profile; sentence=r.sentence;
    if(r.arduino) arduinoState=r.arduino;
    render();
    rArduinoStatus();
  });
}

// ── Render principale ─────────────────────────────────
function render(){
  if(!profile||!sentence)return;
  rSentence(); rCats();
}

function rSentence(){
  const el=document.getElementById('sentenceEl'),v=sentence.vocables;
  const pts=[
    {t:'IDENTIFIED AS ',p:1},{c:'bio'},
    {t:', LOCATED IN ',p:1},{c:'geo'},
    {t:', WORKING AS ',p:1},{c:'prof'},
    {t:', VALUED AS ',p:1},{c:'econ'},
    {t:', NETWORKED WITHIN ',p:1},{c:'socio'},
    {t:', AND EXHIBITING ',p:1},{c:'psycho'},
    {t:'.',p:1}
  ];
  el.innerHTML='';
  pts.forEach(pt=>{
    if(pt.p){el.appendChild(document.createTextNode(pt.t));return;}
    const voc=v[pt.c],nil=voc===null||voc===undefined;
    const s=document.createElement('span');s.className='voc'+(nil?' unresolved':'');
    if(!nil){const st=profile.categories[pt.c];
      if(st.isDecontextualized)s.classList.add('decontext');
      else if(st.poisonLevel>0) s.classList.add('poisoned');
      else if(st.amplifyLevel>0)s.classList.add('amplified');
    }
    s.innerHTML=`<span class="vb">[</span><span class="vi">${nil?'?':voc}</span><span class="vb">]</span>`;
    el.appendChild(s);
  });
}

function rCats(){
  const g=document.getElementById('catGrid');g.innerHTML='';
  CAT_ORDER.forEach(cat=>{
    const st=profile.categories[cat],voc=sentence.vocables[cat],nil=voc===null||voc===undefined;
    const c=document.createElement('div');c.className='island';
    if(nil)c.classList.add('nil');
    let badge=`<span class="isl-badge">STABLE</span>`;
    if(nil) badge=`<span class="isl-badge inactive">AWAITING</span>`;
    else if(st.poisonLevel>0)      badge=`<span class="isl-badge poi">POISONED</span>`;
    else if(st.amplifyLevel>0)     badge=`<span class="isl-badge amp">AMPLIFIED</span>`;
    else if(st.weight<0.3) badge=`<span class="isl-badge inactive">UNSTABLE</span>`;
    let dcCount=0;
    if(!nil && st.isDecontextualized){
      dcCount=(st.dataPoints||[]).filter(dp=>dp.decontextualized).length||1;
    }
    const dcLine=dcCount>0?`<div class="isl-dc-count">${dcCount} DECONTEXTUALIZED</div>`:'';
    c.innerHTML=`<div class="isl-head"><span class="isl-lbl">${CAT_LABELS[cat].toUpperCase()}</span>${badge}</div><div class="isl-val">${nil?'—':voc}</div>${dcLine}`;
    if(!nil)c.addEventListener('click',()=>chrome.tabs.create({url:'https://alessialarocca.github.io/replica/replica-webapp/index.html'}));
    g.appendChild(c);
  });
}

// ── Arduino panel ─────────────────────────────────────
function initArduinoPanel(){
  const input = document.getElementById('ardIp');
  const btn   = document.getElementById('ardBtn');

  // Carica IP salvato
  chrome.runtime.sendMessage({type:'ARDUINO_STATUS'},r=>{
    if(!r)return;
    arduinoState=r;
    if(r.ip){ input.value=r.ip; }
    rArduinoStatus();
  });

  btn.addEventListener('click',()=>{
    if(arduinoState.connected){
      // Disconnetti
      chrome.runtime.sendMessage({type:'ARDUINO_DISCONNECT'},()=>{
        arduinoState={ connected:false, ip:null };
        rArduinoStatus();
      });
    } else {
      const ip = input.value.trim();
      if(!ip){ rArduinoErr('Enter the device IP'); return; }
      btn.textContent='Connecting...'; btn.disabled=true;
      chrome.runtime.sendMessage({type:'ARDUINO_CONNECT', ip}, r=>{
        btn.disabled=false;
        if(r&&r.ok){
          arduinoState={ connected:true, ip };
          rArduinoStatus();
        } else {
          rArduinoErr(r?.error||'Not reachable. Check IP and WiFi.');
        }
      });
    }
  });

  // Connetti premendo Enter
  input.addEventListener('keydown', e=>{ if(e.key==='Enter') btn.click(); });
}

function rArduinoStatus(){
  const dot    = document.getElementById('ardDot');
  const btn    = document.getElementById('ardBtn');
  const status = document.getElementById('ardStatus');
  const input  = document.getElementById('ardIp');
  if(!dot||!btn||!status)return;

  if(arduinoState.connected){
    dot.className='ard-dot on';
    btn.textContent='Disconnect'; btn.className='ard-btn connected';
    status.textContent='Device connected. Switch active.';
    status.className='ard-status ok';
    if(arduinoState.ip) input.value=arduinoState.ip;
    input.disabled=true;
  } else {
    dot.className='ard-dot';
    btn.textContent='Connect'; btn.className='ard-btn';
    status.textContent='';
    status.className='ard-status mid';
    input.disabled=false;
  }
}

function rArduinoErr(msg){
  const dot    = document.getElementById('ardDot');
  const status = document.getElementById('ardStatus');
  if(dot) dot.className='ard-dot err';
  if(status){ status.textContent=msg; status.className='ard-status err'; }
}
