#!/usr/bin/env node
/* botmon - one line per bot, what it is doing right now, no scrolling.
 *
 *   node botmon.js [container]
 *   node botmon.js --plain [--sort=rate] [--team=K] [--filter=bomb]
 *   node botmon.js --from=sample.txt      replay a captured sample
 *
 * Samples the container once a second: peek for ship state, each bot's
 * decision log for what it decided and how fast it is deciding it.
 *
 * Keys:  left/right sort column   r reverse   t team   / search   q quit
 *
 * The ID column is the ship as players say it: team letter plus slot, so
 * F8, Ra, Ff. Sorting by it groups a team together in slot order.
 */

const blessed = require('blessed');
const { execFile } = require('child_process');
const path = require('path');

const args = process.argv.slice(2);
const flag = name => {
  const a = args.find(x => x.startsWith('--' + name + '='));
  return a ? a.slice(name.length + 3) : null;
};
const PLAIN = args.includes('--plain');   /* one sample, no TUI, tags stripped */
const CONTAINER = args.filter(a => !a.startsWith('--'))[0] ||
                  process.env.CONTAINER || 'vanilla-netrek-server';
const STATE_SH = path.join(__dirname, '..', 'botstate.sh');
const FROM = flag('from');   /* replay a captured sample instead of sampling */

const TEAM = { 0: '-', 1: 'F', 2: 'R', 4: 'K', 8: 'O' };
/* a ship's id as players say it: team letter + slot, slots past 9 continue
 * into letters, so slot 10 on Romulan is "Ra". Same table the client uses. */
const SHIPNOS = '0123456789abcdefghijklmnopqrstuvwxyz';
const shipId = (team, slot) =>
  (team || '?') + (SHIPNOS[slot] !== undefined ? SHIPNOS[slot] : '?');
const TEAMCOL = { F: 'yellow', R: 'red', K: 'green', O: 'cyan', '-': 'white' };
const TEAMS = ['ALL', 'F', 'R', 'K', 'O'];

/* a decision that undoes a decision: counts toward rate, useless as "current" */
const isReset = a => /RESET/.test(a) || /^UN/.test(a);

/* sortable columns, in display order; dir is the default direction */
const SORTS = [
  { key: 'bot',    label: 'BOT',    dir:  1, get: r => r.name.toLowerCase() },
  { key: 'team',   label: 'ID',     dir:  1, get: r => (r.ship || {}).id || '' },
  { key: 'action', label: 'ACTION', dir:  1, get: r => r.sum.cur.action },
  { key: 'target', label: 'TARGET', dir:  1, get: r => r.sum.cur.detail },
  { key: 'for',    label: 'FOR',    dir: -1, get: r => r.sum.dwell },
  { key: 'rate',   label: 'RATE',   dir: -1, get: r => r.sum.rate },
  { key: 'arm',    label: 'ARM',    dir: -1, get: r => (r.ship || {}).arm || 0 },
];

const view = {
  sort: Math.max(0, SORTS.findIndex(s => s.key === (flag('sort') || 'for'))),
  reverse: false,
  team: (flag('team') || 'ALL').toUpperCase(),
  filter: (flag('filter') || '').toLowerCase(),
};

let screen = null, box = null, statusbar = null, prompt = null;

if (!PLAIN) {
  screen = blessed.screen({ smartCSR: true, title: 'netrek botmon' });
  box = blessed.box({
    parent: screen, top: 0, left: 0, width: '100%', height: '100%-1',
    tags: true, padding: { left: 1, right: 1 },
  });
  statusbar = blessed.box({
    parent: screen, bottom: 0, left: 0, width: '100%', height: 1,
    tags: true, padding: { left: 1, right: 1 },
  });
  prompt = blessed.textbox({
    parent: screen, bottom: 0, left: 0, width: '100%', height: 1,
    hidden: true, inputOnFocus: true, style: { bg: 'blue' },
  });

  screen.key(['q', 'C-c'], () => process.exit(0));
  screen.key('right', () => { view.sort = (view.sort + 1) % SORTS.length; redraw(); });
  screen.key('left',  () => { view.sort = (view.sort + SORTS.length - 1) % SORTS.length; redraw(); });
  screen.key('r', () => { view.reverse = !view.reverse; redraw(); });
  screen.key('t', () => {
    view.team = TEAMS[(TEAMS.indexOf(view.team) + 1) % TEAMS.length];
    redraw();
  });
  screen.key('escape', () => { view.filter = ''; redraw(); });
  screen.key('/', () => {
    prompt.show();
    prompt.setValue('');
    prompt.setLabel && prompt.setLabel('');
    prompt.readInput(() => {});
    screen.render();
  });
  prompt.on('submit', value => {
    view.filter = String(value || '').trim().toLowerCase();
    prompt.hide();
    screen.focusPop && screen.focusPop();
    redraw();
  });
  prompt.on('cancel', () => { prompt.hide(); redraw(); });
}

let lastError = '';
let lastData = null;

function sample(cb) {
  if (FROM) {
    try { return cb(require('fs').readFileSync(FROM, 'utf8')); }
    catch (e) { lastError = String(e.message); return cb(null); }
  }
  execFile('docker', ['exec', CONTAINER, 'bash', '/tmp/botstate.sh'],
    { maxBuffer: 32 * 1024 * 1024 }, (err, stdout) => {
      if (err) { lastError = String(err.message).split('\n')[0]; return cb(null); }
      lastError = '';
      cb(stdout);
    });
}

function parse(out) {
  const parts = out.split('=== DECISIONS');
  const shipsRaw = parts[0] || '';
  const decRaw = parts[1] || '';
  const ships = {}, humans = [];
  let tourn = 0;

  for (const line of shipsRaw.split('\n')) {
    const t = line.match(/^tourn=(\d+)/);
    if (t) { tourn = Number(t[1]); continue; }
    const m = line.match(/^\s*(\d+)\s+(\S+)\s+(bot|mgr|HUMAN)\s+team=(\d+)\s+flags=0x([0-9a-f]+)((?:\s+(?:CLOAK|ORBIT))*)\s+x=\s*(-?\d+)\s+y=\s*(-?\d+)\s+arm=(\d+)\s+seen=(\d+)(.*)$/);
    if (!m) continue;
    const s = {
      slot: Number(m[1]), name: m[2], kind: m[3], team: TEAM[Number(m[4])] || '?',
      id: shipId(TEAM[Number(m[4])], Number(m[1])),
      cloak: /CLOAK/.test(m[6]), orbit: /ORBIT/.test(m[6]),
      x: Number(m[7]), y: Number(m[8]), arm: Number(m[9]), seen: Number(m[10]),
      tail: m[11].trim(),
    };
    const near = s.tail.match(/nearest bot (\S+) at (\d+) \(range (\d+)\) -> (\w+)/);
    if (near) {
      s.near = { name: near[1], dist: Number(near[2]), range: Number(near[3]),
                 visible: near[4] === 'VISIBLE' };
    }
    if (s.kind === 'HUMAN') humans.push(s); else ships[s.name] = s;
  }

  const bots = {};
  let cur = null, maxT = 0;
  const secs = hms => {
    const p = hms.split(':').map(Number);
    return p[0] * 3600 + p[1] * 60 + p[2];
  };
  for (const line of decRaw.split('\n')) {
    if (line.startsWith('@@ ')) { cur = line.slice(3).trim(); bots[cur] = []; continue; }
    if (!cur || line.indexOf(' DECIDE ') < 0) continue;
    const t = secs(line.slice(0, 8));
    if (t > maxT) maxT = t;
    bots[cur].push({
      t,
      action: line.slice(16, 29).trim(),
      detail: line.slice(30, 46).trim(),
      reason: line.slice(47).trim(),
    });
  }
  return { ships, humans, bots, tourn, now: maxT };
}

function summarise(list, now) {
  if (!list || !list.length) return null;
  const rate = list.filter(d => now - d.t <= 10).length / 10;

  /* current action: most recent decision that is not an undo */
  let i = list.length - 1;
  while (i >= 0 && isReset(list[i].action)) i--;
  if (i < 0) i = list.length - 1;
  const cur = list[i];

  /* how long that same action+target has run, ignoring undos it alternates with */
  const key = d => d.action + ' ' + d.detail;
  let start = cur.t;
  for (let j = i - 1; j >= 0; j--) {
    if (isReset(list[j].action)) continue;
    if (key(list[j]) !== key(cur)) break;
    start = list[j].t;
  }
  return { cur, rate, dwell: now - start, thrash: rate >= 2 };
}

const pad = (s, n) => String(s === undefined || s === null ? '' : s).slice(0, n).padEnd(n);
const dur = s => (s >= 60 ? Math.floor(s / 60) + 'm' + String(s % 60).padStart(2, '0') : s + 's');

function rowsFor(data) {
  const rows = Object.keys(data.bots).map(name => ({
    name, ship: data.ships[name], sum: summarise(data.bots[name], data.now),
  })).filter(r => r.sum);

  const shown = rows.filter(r => {
    if (view.team !== 'ALL' && (r.ship || {}).team !== view.team) return false;
    if (!view.filter) return true;
    const hay = (r.name + ' ' + r.sum.cur.action + ' ' + r.sum.cur.detail + ' ' +
                 r.sum.cur.reason).toLowerCase();
    return hay.indexOf(view.filter) >= 0;
  });

  const col = SORTS[view.sort];
  const dir = (col.dir || 1) * (view.reverse ? -1 : 1);
  shown.sort((a, b) => {
    const x = col.get(a), y = col.get(b);
    if (x === y) return a.name.localeCompare(b.name);
    return (x > y ? 1 : -1) * dir;
  });
  return { rows, shown };
}

function buildLines(data) {
  if (!data) return ['{red-fg}cannot sample ' + CONTAINER + '{/}', '', lastError];
  const L = [];
  const { humans, tourn } = data;
  const { rows, shown } = rowsFor(data);
  const col = SORTS[view.sort];

  L.push('{bold}netrek botmon{/}  ' + shown.length +
         (shown.length === rows.length ? '' : '/' + rows.length) +
         ' bots   tourn=' + tourn);
  L.push('');

  /* header, with the active sort column marked */
  const head = [
    pad('BOT', 13), pad('ID', 4), pad('ACTION', 14), pad('TARGET', 17),
    pad('FOR', 7), pad('RATE', 7), pad('ARM', 4),
  ];
  const arrow = ((col.dir || 1) * (view.reverse ? -1 : 1)) > 0 ? '+' : '-';
  const marked = head.map((h, i) => (SORTS[i] && i === view.sort)
    ? '{inverse}' + h.replace(/(\s*)$/, arrow + '$1').slice(0, h.length) + '{/inverse}'
    : h);
  L.push('{bold}' + marked.join('') + pad('ST', 4) + pad('POSITION', 14) + 'REASON{/}');

  for (const r of shown) {
    const s = r.ship || {};
    const tcol = TEAMCOL[s.team] || 'white';
    const st = (s.cloak ? 'C' : '-') + (s.orbit ? 'O' : '-');
    const rate = r.sum.rate >= 0.1 ? r.sum.rate.toFixed(1) + '/s' : '';
    L.push(
      pad(r.name, 13) +
      '{' + tcol + '-fg}' + pad(s.id || '?', 4) + '{/}' +
      pad(r.sum.cur.action, 14) +
      pad(r.sum.cur.detail, 17) +
      pad(dur(r.sum.dwell), 7) +
      (r.sum.thrash ? '{red-fg}' : '') + pad(rate, 7) + (r.sum.thrash ? '{/}' : '') +
      pad(s.arm || 0, 4) +
      (s.orbit ? '{yellow-fg}' : '') + pad(st, 4) + (s.orbit ? '{/}' : '') +
      pad(s.x !== undefined ? s.x + ',' + s.y : '', 14) +
      r.sum.cur.reason);
  }

  if (!shown.length) L.push('{gray-fg}nothing matches{/}');

  /* always shown, whatever the filter: this is your own line */
  if (humans.length) {
    L.push('');
    L.push('{bold}YOU{/}');
    for (const h of humans) {
      const n = h.near;
      let sight;
      if (!n) {
        sight = '{gray-fg}no enemy bots{/}';
      } else if (n.visible) {
        sight = '{red-fg}{bold}VISIBLE{/} to ' + n.name + ' at ' + n.dist +
                ' (sees ' + n.range + ')';
      } else {
        sight = '{green-fg}{bold}HIDDEN{/}  nearest ' + n.name + ' at ' + n.dist +
                ', needs ' + n.range;
      }
      L.push(pad(h.name, 13) + '{' + TEAMCOL[h.team] + '-fg}' + pad(h.id || h.team, 4) + '{/}' +
             /* pad the visible text, then wrap it: tags are not characters */
             (h.cloak ? '{cyan-fg}' : '') + pad(h.cloak ? 'CLOAKED' : 'uncloaked', 10) +
             (h.cloak ? '{/}' : '') +
             pad(h.orbit ? 'ORBIT' : '', 6) +
             pad('arm=' + h.arm, 7) + sight);
    }
  }

  const thrashers = shown.filter(r => r.sum.thrash);
  if (thrashers.length) {
    L.push('');
    L.push('{red-fg}' + thrashers.length + ' bot(s) thrashing{/}: ' +
           thrashers.map(t => t.name + ' ' + t.sum.cur.action + ' ' + t.sum.cur.detail).join(', '));
  }
  return L;
}

function statusLine() {
  const col = SORTS[view.sort];
  const arrow = ((col.dir || 1) * (view.reverse ? -1 : 1)) > 0 ? '↑' : '↓';
  return '{black-fg}{white-bg} sort:' + col.label + arrow +
         '  team:' + view.team +
         '  search:' + (view.filter || '-') +
         '  {/}{gray-fg} ←→ sort  r reverse  t team  / search  esc clear  q quit{/}';
}

const strip = s => s.replace(/\{[^}]*\}/g, '');

function redraw() {
  if (PLAIN || !lastData) return render(lastData);
  box.setContent(buildLines(lastData).join('\n'));
  statusbar.setContent(statusLine());
  screen.render();
}

function render(data) {
  if (data) lastData = data;
  const L = buildLines(data);
  if (PLAIN) { console.log(L.map(strip).join('\n')); process.exit(data ? 0 : 1); }
  box.setContent(L.join('\n'));
  statusbar.setContent(statusLine());
  screen.render();
}

function start() {
  const tick = () => sample(out => render(out ? parse(out) : null));
  tick();
  if (!PLAIN) setInterval(tick, 1000);
}

/* keep the sampler in the container fresh: it dies with a recreate */
if (FROM) start();
else execFile('docker', ['cp', STATE_SH, CONTAINER + ':/tmp/botstate.sh'], start);
