#!/usr/bin/env node
/* botmon - one line per bot, what it is doing right now, no scrolling.
 *
 *   node botmon.js [container]
 *
 * Samples the container once a second: peek for ship state, each bot's
 * decision log for what it decided and how fast it is deciding it.
 */

const blessed = require('blessed');
const { execFile } = require('child_process');
const path = require('path');

const args = process.argv.slice(2);
const PLAIN = args.includes('--plain');   /* one sample, no TUI, tags stripped */
const CONTAINER = args.filter(a => !a.startsWith('--'))[0] ||
                  process.env.CONTAINER || 'vanilla-netrek-server';
const STATE_SH = path.join(__dirname, '..', 'botstate.sh');

const TEAM = { 0: '-', 1: 'F', 2: 'R', 4: 'K', 8: 'O' };
const TEAMCOL = { F: 'yellow', R: 'red', K: 'green', O: 'cyan', '-': 'white' };

/* a decision that undoes a decision: counts toward rate, useless as "current" */
const isReset = a => /RESET/.test(a) || /^UN/.test(a);

const screen = PLAIN ? null : blessed.screen({ smartCSR: true, title: 'netrek botmon' });
const box = PLAIN ? null : blessed.box({
  parent: screen, top: 0, left: 0, width: '100%', height: '100%',
  tags: true, padding: { left: 1, right: 1 },
});
if (screen) screen.key(['q', 'escape', 'C-c'], () => process.exit(0));

let lastError = '';

function sample(cb) {
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
      cloak: /CLOAK/.test(m[6]), orbit: /ORBIT/.test(m[6]),
      x: Number(m[7]), y: Number(m[8]), arm: Number(m[9]), seen: Number(m[10]),
      tail: m[11].trim(),
    };
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

function buildLines(data) {
  const L = [];
  if (!data) return ['{red-fg}cannot sample ' + CONTAINER + '{/}', '', lastError];
  const { ships, humans, bots, tourn, now } = data;

  const rows = Object.keys(bots).map(name => ({
    name, ship: ships[name], sum: summarise(bots[name], now),
  })).filter(r => r.sum);
  rows.sort((a, b) => b.sum.dwell - a.sum.dwell);

  L.push('{bold}netrek botmon{/}  ' + rows.length + ' bots   tourn=' + tourn +
         '   {gray-fg}sorted by time in current action, q to quit{/}');
  L.push('');
  L.push('{bold}' + pad('BOT', 13) + pad('T', 2) + pad('ACTION', 14) +
         pad('TARGET', 17) + pad('FOR', 7) + pad('RATE', 7) + pad('ARM', 4) +
         pad('ST', 4) + pad('POSITION', 14) + 'REASON{/}');

  for (const r of rows) {
    const s = r.ship || {};
    const tcol = TEAMCOL[s.team] || 'white';
    const st = (s.cloak ? 'C' : '-') + (s.orbit ? 'O' : '-');
    const rate = r.sum.rate >= 0.1 ? r.sum.rate.toFixed(1) + '/s' : '';
    L.push(
      pad(r.name, 13) +
      '{' + tcol + '-fg}' + pad(s.team || '?', 2) + '{/}' +
      pad(r.sum.cur.action, 14) +
      pad(r.sum.cur.detail, 17) +
      pad(dur(r.sum.dwell), 7) +
      (r.sum.thrash ? '{red-fg}' : '') + pad(rate, 7) + (r.sum.thrash ? '{/}' : '') +
      pad(s.arm || 0, 4) +
      (s.orbit ? '{yellow-fg}' : '') + pad(st, 4) + (s.orbit ? '{/}' : '') +
      pad(s.x !== undefined ? s.x + ',' + s.y : '', 14) +
      r.sum.cur.reason);
  }

  if (humans.length) {
    L.push('');
    L.push('{bold}HUMANS{/}');
    for (const h of humans) {
      L.push(pad(h.name, 13) + '{' + TEAMCOL[h.team] + '-fg}' + pad(h.team, 2) + '{/}' +
             pad((h.cloak ? 'CLOAK ' : '') + (h.orbit ? 'ORBIT' : ''), 14) +
             pad('arm=' + h.arm, 8) + pad('seen=' + h.seen, 8) + h.tail);
    }
  }

  const thrashers = rows.filter(r => r.sum.thrash);
  if (thrashers.length) {
    L.push('');
    L.push('{red-fg}' + thrashers.length + ' bot(s) thrashing{/}: ' +
           thrashers.map(t => t.name + ' ' + t.sum.cur.action + ' ' + t.sum.cur.detail).join(', '));
  }

  return L;
}

const strip = s => s.replace(/\{[^}]*\}/g, '');

function render(data) {
  const L = buildLines(data);
  if (PLAIN) { console.log(L.map(strip).join('\n')); process.exit(data ? 0 : 1); }
  box.setContent(L.join('\n'));
  screen.render();
}

/* keep the sampler in the container fresh: it dies with a recreate */
execFile('docker', ['cp', STATE_SH, CONTAINER + ':/tmp/botstate.sh'], () => {
  const tick = () => sample(out => render(out ? parse(out) : null));
  tick();
  if (!PLAIN) setInterval(tick, 1000);
});
