// =====================================================================
//  Panel del staff - Minecraft SS Tool (Supabase + GitHub Pages)
//  Todo el texto que viene del programa se muestra con textContent
//  (nunca innerHTML) para que un reporte no pueda inyectar código.
// =====================================================================
const cfg = window.SS_CONFIG || {};
const $ = (sel) => document.querySelector(sel);
const show = (el, yes = true) => el.classList.toggle('hidden', !yes);

function h(tag, attrs = {}, ...children) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === null || v === undefined || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'text') el.textContent = v;
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const c of children.flat()) {
    if (c === null || c === undefined || c === false) continue;
    el.append(c instanceof Node ? c : String(c));
  }
  return el;
}
const fmtDate = (s) => (s ? new Date(s).toLocaleString('es', { dateStyle: 'short', timeStyle: 'short' }) : '—');
const badge = (text, color) => h('span', { class: `badge ${color}`, text });
const alertBadge = (n) => (n > 0 ? badge(`${n} alerta${n === 1 ? '' : 's'}`, 'red') : badge('Limpio', 'green'));

let toastTimer;
function toast(msg, bad = false) {
  const t = $('#toast');
  t.textContent = msg;
  t.className = 'toast' + (bad ? ' bad' : '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => show(t, false), 3500);
}
function messageRow(tbody, cols, text, isError = false) {
  tbody.replaceChildren(h('tr', {}, h('td', { colspan: cols, class: isError ? 'error' : 'muted', text })));
}
async function copy(text) {
  try { await navigator.clipboard.writeText(text); toast('Copiado: ' + text); } catch { toast('No se pudo copiar', true); }
}

// ------------------------------ arranque ------------------------------
let sb = null;
let entered = false;

if (!cfg.SUPABASE_URL || !cfg.SUPABASE_ANON_KEY || !window.supabase) {
  show($('#setup'));
} else {
  sb = window.supabase.createClient(cfg.SUPABASE_URL, cfg.SUPABASE_ANON_KEY);
  sb.auth.onAuthStateChange((_event, session) => {
    // se difiere para no llamar a Supabase dentro del callback
    setTimeout(() => (session ? enterApp(session) : enterLogin()), 0);
  });
  wireEvents();
}

function enterLogin() {
  entered = false;
  show($('#app'), false);
  show($('#login'));
}
function enterApp(session) {
  show($('#login'), false);
  show($('#app'));
  $('#user-email').textContent = session.user.email || '';
  if (!entered) { entered = true; openTab('scans'); }
}

function wireEvents() {
  $('#login-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    $('#login-error').textContent = '';
    const { error } = await sb.auth.signInWithPassword({
      email: $('#login-email').value.trim(),
      password: $('#login-pass').value,
    });
    if (error) $('#login-error').textContent = 'No se pudo entrar: ' + error.message;
  });
  $('#logout').addEventListener('click', () => sb.auth.signOut());
  document.querySelectorAll('[data-tab]').forEach((b) => b.addEventListener('click', () => openTab(b.dataset.tab)));

  $('#scan-search').addEventListener('input', renderScans);
  $('#scan-refresh').addEventListener('click', loadScans);
  $('#lic-refresh').addEventListener('click', loadLicenses);
  $('#rule-refresh').addEventListener('click', loadRules);
  $('#lic-form').addEventListener('submit', createLicenses);
  $('#rule-form').addEventListener('submit', createRule);

  $('#m-close').addEventListener('click', closeModal);
  $('#modal').addEventListener('click', (e) => { if (e.target.id === 'modal') closeModal(); });
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeModal(); });
}

function openTab(name) {
  for (const t of ['scans', 'lic', 'rules']) show($('#tab-' + t), t === name);
  document.querySelectorAll('[data-tab]').forEach((b) => b.classList.toggle('active', b.dataset.tab === name));
  if (name === 'scans') loadScans();
  if (name === 'lic') loadLicenses();
  if (name === 'rules') loadRules();
}

// ------------------------------ escaneos ------------------------------
let scansCache = [];

async function loadScans() {
  const tbody = $('#scans-body');
  messageRow(tbody, 6, 'Cargando…');
  const { data, error } = await sb
    .from('scans')
    .select('id,created_at,pc_name,windows_user,license_key,alerts,tool_version')
    .order('created_at', { ascending: false })
    .limit(500);
  if (error) return messageRow(tbody, 6, 'Error: ' + error.message, true);
  scansCache = data || [];
  renderStats();
  renderScans();
}

function renderStats() {
  const today = new Date().toDateString();
  const withAlerts = scansCache.filter((s) => s.alerts > 0).length;
  $('#st-total').textContent = scansCache.length;
  $('#st-alerts').textContent = withAlerts;
  $('#st-clean').textContent = scansCache.length - withAlerts;
  $('#st-today').textContent = scansCache.filter((s) => new Date(s.created_at).toDateString() === today).length;
}

function renderScans() {
  const tbody = $('#scans-body');
  const q = $('#scan-search').value.trim().toLowerCase();
  const rows = scansCache.filter((s) => !q || [s.pc_name, s.windows_user, s.license_key]
    .some((v) => (v || '').toLowerCase().includes(q)));
  if (!rows.length) return messageRow(tbody, 6, scansCache.length ? 'Sin resultados para esa búsqueda.' : 'Todavía no hay escaneos.');
  tbody.replaceChildren(...rows.map((s) => h('tr', { class: 'clickable', onclick: () => openScan(s.id) },
    h('td', { text: fmtDate(s.created_at) }),
    h('td', { text: s.pc_name || '—' }),
    h('td', { text: s.windows_user || '—' }),
    h('td', {}, h('code', { text: s.license_key || '—' })),
    h('td', {}, alertBadge(s.alerts)),
    h('td', { class: 'right' }, h('button', { class: 'btn small', text: 'Ver' })),
  )));
}

let currentScan = null;
async function openScan(id) {
  show($('#modal'));
  $('#m-title').textContent = 'Cargando…';
  $('#m-meta').textContent = '';
  $('#m-body').replaceChildren();
  const { data, error } = await sb.from('scans').select('*').eq('id', id).single();
  if (error) { $('#m-title').textContent = 'Error'; $('#m-meta').textContent = error.message; return; }
  currentScan = data;
  $('#m-title').textContent = `${data.pc_name || 'PC'} · ${data.windows_user || ''}`;
  $('#m-meta').textContent = `${fmtDate(data.created_at)}  ·  Licencia ${data.license_key || '—'}  ·  v${data.tool_version || '?'}  ·  HWID ${data.hwid || '—'}`;

  const flags = Array.isArray(data.flags) ? data.flags : [];
  const body = [
    h('h2', {}, 'Alertas ', alertBadge(data.alerts)),
    flags.length ? h('ul', { class: 'flags' }, flags.map((f) => h('li', { text: String(f) })))
                 : h('p', { class: 'green', text: 'No se encontró nada sospechoso.' }),
    h('h2', { text: 'Reporte completo' }),
    h('pre', { class: 'report', text: data.report || '(vacío)' }),
  ];
  $('#m-body').replaceChildren(...body);
  $('#m-download').onclick = () => {
    const blob = new Blob([data.report || ''], { type: 'text/plain;charset=utf-8' });
    const a = h('a', { href: URL.createObjectURL(blob), download: `SS_${(data.pc_name || 'pc')}_${data.created_at.slice(0, 10)}.txt` });
    document.body.append(a); a.click(); a.remove();
  };
  $('#m-delete').onclick = async () => {
    if (!confirm('¿Eliminar este escaneo? No se puede deshacer.')) return;
    const { error: e } = await sb.from('scans').delete().eq('id', data.id);
    if (e) return toast('Error: ' + e.message, true);
    toast('Escaneo eliminado');
    closeModal();
    loadScans();
  };
}
function closeModal() { show($('#modal'), false); currentScan = null; }

// ------------------------------ licencias ------------------------------
function genKey() {
  const abc = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
  const b = new Uint8Array(12);
  crypto.getRandomValues(b);
  let s = '';
  for (let i = 0; i < 12; i++) { if (i && i % 4 === 0) s += '-'; s += abc[b[i] % abc.length]; }
  return 'SS-' + s;
}

function licStatus(l) {
  if (!l.active) return badge('Desactivada', 'gray');
  if (l.expires_at && new Date(l.expires_at) < new Date()) return badge('Vencida', 'orange');
  if (l.max_uses && l.uses >= l.max_uses) return badge('Agotada', 'orange');
  return badge('Activa', 'green');
}

async function loadLicenses() {
  const tbody = $('#lic-body');
  messageRow(tbody, 7, 'Cargando…');
  const { data, error } = await sb.from('licenses').select('*').order('created_at', { ascending: false });
  if (error) return messageRow(tbody, 7, 'Error: ' + error.message, true);
  if (!data.length) return messageRow(tbody, 7, 'Todavía no hay licencias. Crea una arriba.');
  tbody.replaceChildren(...data.map((l) => h('tr', {},
    h('td', {}, h('code', { text: l.key }), ' ', h('button', { class: 'btn small', text: 'Copiar', onclick: () => copy(l.key) })),
    h('td', { text: l.note || '—' }),
    h('td', { text: `${l.uses} / ${l.max_uses ?? '∞'}` }),
    h('td', { text: l.expires_at ? fmtDate(l.expires_at) : 'No vence' }),
    h('td', { class: 'muted', text: l.lock_hwid ? (l.hwid ? l.hwid.slice(0, 8) + '…' : 'Libre (se vincula al usarla)') : '—' }),
    h('td', {}, licStatus(l)),
    h('td', { class: 'right' },
      l.hwid ? h('button', { class: 'btn small', text: 'Liberar PC', onclick: () => updateLicense(l.id, { hwid: null }) }) : null,
      h('button', { class: 'btn small', text: l.active ? 'Desactivar' : 'Activar', onclick: () => updateLicense(l.id, { active: !l.active }) }),
      h('button', { class: 'btn small danger', text: 'Borrar', onclick: () => deleteRow('licenses', l.id, loadLicenses) }),
    ),
  )));
}

async function createLicenses(e) {
  e.preventDefault();
  const qty = Math.min(50, Math.max(1, parseInt($('#lic-qty').value || '1', 10)));
  const maxUses = $('#lic-uses').value ? Math.max(1, parseInt($('#lic-uses').value, 10)) : null;
  const exp = $('#lic-exp').value ? new Date($('#lic-exp').value + 'T23:59:59').toISOString() : null;
  const rows = Array.from({ length: qty }, () => ({
    key: genKey(),
    note: $('#lic-note').value.trim() || null,
    max_uses: maxUses,
    expires_at: exp,
    lock_hwid: $('#lic-lock').checked,
  }));
  const { error } = await sb.from('licenses').insert(rows);
  if (error) return toast('Error: ' + error.message, true);
  const box = $('#lic-created');
  box.replaceChildren(h('b', { text: `${qty} licencia${qty === 1 ? '' : 's'} creada${qty === 1 ? '' : 's'}:` }),
    ...rows.map((r) => h('div', {}, h('code', { text: r.key }), h('button', { class: 'btn small', type: 'button', text: 'Copiar', onclick: () => copy(r.key) }))));
  show(box);
  $('#lic-form').reset();
  $('#lic-qty').value = 1;
  loadLicenses();
}

async function updateLicense(id, patch) {
  const { error } = await sb.from('licenses').update(patch).eq('id', id);
  if (error) return toast('Error: ' + error.message, true);
  loadLicenses();
}

async function deleteRow(table, id, reload) {
  if (!confirm('¿Seguro que quieres borrarlo?')) return;
  const { error } = await sb.from(table).delete().eq('id', id);
  if (error) return toast('Error: ' + error.message, true);
  reload();
}

// ------------------------------ reglas ------------------------------
const typeLabel = { mod: ['Mod', 'purple'], web: ['Página web', 'orange'], process: ['Programa', 'gray'] };

async function loadRules() {
  const tbody = $('#rules-body');
  messageRow(tbody, 5, 'Cargando…');
  const { data, error } = await sb.from('rules').select('*').order('created_at', { ascending: false });
  if (error) return messageRow(tbody, 5, 'Error: ' + error.message, true);
  if (!data.length) return messageRow(tbody, 5, 'Sin reglas extra. El programa usa su lista base.');
  tbody.replaceChildren(...data.map((r) => {
    const [label, color] = typeLabel[r.type] || [r.type, 'gray'];
    return h('tr', {},
      h('td', {}, badge(label, color)),
      h('td', { text: r.name }),
      h('td', {}, h('code', { text: r.tokens })),
      h('td', {}, r.active ? badge('Activa', 'green') : badge('Pausada', 'gray')),
      h('td', { class: 'right' },
        h('button', { class: 'btn small', text: r.active ? 'Pausar' : 'Activar', onclick: async () => {
          const { error: e } = await sb.from('rules').update({ active: !r.active }).eq('id', r.id);
          if (e) toast('Error: ' + e.message, true); else loadRules();
        } }),
        h('button', { class: 'btn small danger', text: 'Borrar', onclick: () => deleteRow('rules', r.id, loadRules) }),
      ),
    );
  }));
}

async function createRule(e) {
  e.preventDefault();
  const tokens = $('#rule-tokens').value.split(',').map((t) => t.trim().toLowerCase()).filter(Boolean).join(',');
  if (!tokens) return toast('Escribe al menos una palabra', true);
  const { error } = await sb.from('rules').insert({
    type: $('#rule-type').value,
    name: $('#rule-name').value.trim().replace(/\|/g, '/'),
    tokens: tokens.replace(/\|/g, ''),
  });
  if (error) return toast('Error: ' + error.message, true);
  toast('Regla agregada');
  $('#rule-form').reset();
  loadRules();
}
